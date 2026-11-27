/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "COLRFonts.h"

#include <limits>
#include <numbers>

#include "TextDrawTarget.h"
#include "gfxFontUtils.h"
#include "gfxUtils.h"
#include "harfbuzz/hb-ot.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/gfx/Helpers.h"
#include "nsHashtablesFwd.h"

using namespace mozilla;
using namespace mozilla::gfx;

namespace {  // anonymous namespace for implementation internals

#pragma pack(1)  // ensure no padding is added to the COLR structs

// Alias bigendian-reading types from gfxFontUtils to names used in the spec.
using int16 = AutoSwap_PRInt16;
using uint16 = AutoSwap_PRUint16;
using int32 = AutoSwap_PRInt32;
using uint32 = AutoSwap_PRUint32;
using FWORD = AutoSwap_PRInt16;
using UFWORD = AutoSwap_PRUint16;
using Offset16 = AutoSwap_PRUint16;
using Offset24 = AutoSwap_PRUint24;
using Offset32 = AutoSwap_PRUint32;

struct COLRv1Header;
struct ClipList;
struct LayerRecord;
struct BaseGlyphRecord;
struct DeltaSetIndexMap;
struct ItemVariationStore;

struct COLRHeader {
  uint16 version;
  uint16 numBaseGlyphRecords;
  Offset32 baseGlyphRecordsOffset;
  Offset32 layerRecordsOffset;
  uint16 numLayerRecords;

  const BaseGlyphRecord* GetBaseGlyphRecords() const {
    return reinterpret_cast<const BaseGlyphRecord*>(
        reinterpret_cast<const char*>(this) + baseGlyphRecordsOffset);
  }

  const LayerRecord* GetLayerRecords() const {
    return reinterpret_cast<const LayerRecord*>(
        reinterpret_cast<const char*>(this) + layerRecordsOffset);
  }

  bool Validate(uint64_t aLength) const;
};

struct BaseGlyphPaintRecord {
  uint16 glyphID;
  Offset32 paintOffset;
};

struct BaseGlyphList {
  uint32 numBaseGlyphPaintRecords;
  // BaseGlyphPaintRecord baseGlyphPaintRecords[numBaseGlyphPaintRecords];
  const BaseGlyphPaintRecord* baseGlyphPaintRecords() const {
    return reinterpret_cast<const BaseGlyphPaintRecord*>(this + 1);
  }
  bool Validate(const COLRv1Header* aHeader, uint64_t aLength) const;
};

struct LayerList {
  uint32 numLayers;
  // uint32 paintOffsets[numLayers];
  const uint32* paintOffsets() const {
    return reinterpret_cast<const uint32*>(this + 1);
  }
  bool Validate(const COLRv1Header* aHeader, uint64_t aLength) const;
};

struct COLRv1Header {
  COLRHeader base;
  Offset32 baseGlyphListOffset;
  Offset32 layerListOffset;
  Offset32 clipListOffset;
  Offset32 varIndexMapOffset;
  Offset32 itemVariationStoreOffset;

  bool Validate(uint64_t aLength) const;

  const BaseGlyphList* baseGlyphList() const {
    uint32_t offset = baseGlyphListOffset;
    if (!offset) {
      return nullptr;
    }
    const char* ptr = reinterpret_cast<const char*>(this) + offset;
    return reinterpret_cast<const struct BaseGlyphList*>(ptr);
  }

  const LayerList* layerList() const {
    uint32_t offset = layerListOffset;
    if (!offset) {
      return nullptr;
    }
    const char* ptr = reinterpret_cast<const char*>(this) + offset;
    return reinterpret_cast<const LayerList*>(ptr);
  }

  const struct ClipList* clipList() const {
    uint32_t offset = clipListOffset;
    if (!offset) {
      return nullptr;
    }
    const char* ptr = reinterpret_cast<const char*>(this) + offset;
    return reinterpret_cast<const ClipList*>(ptr);
  }

  const struct DeltaSetIndexMap* varIndexMap() const {
    uint32_t offset = varIndexMapOffset;
    if (!offset) {
      return nullptr;
    }
    const char* ptr = reinterpret_cast<const char*>(this) + offset;
    return reinterpret_cast<const DeltaSetIndexMap*>(ptr);
  }

  const struct ItemVariationStore* itemVariationStore() const {
    uint32_t offset = itemVariationStoreOffset;
    if (!offset) {
      return nullptr;
    }
    const char* ptr = reinterpret_cast<const char*>(this) + offset;
    return reinterpret_cast<const ItemVariationStore*>(ptr);
  }

  const BaseGlyphPaintRecord* GetBaseGlyphPaint(uint32_t aGlyphId) const;
};

struct PaintState {
  union {
    const COLRHeader* v0;
    const COLRv1Header* v1;
  } mHeader;
  const hb_color_t* mPalette;
  DrawTarget* mDrawTarget;
  ScaledFont* mScaledFont;
  const int* mCoords;
  DrawOptions mDrawOptions;
  uint32_t mCOLRLength;
  sRGBColor mCurrentColor;
  float mFontUnitsToPixels;
  uint16_t mNumColors;
  uint16_t mCoordCount;
  nsTArray<uint32_t>* mVisited;
  nsTHashMap<uint32_t, Rect>* mBoundsCache;
  bool* mUsedCurrentColor = nullptr;

  const uint8_t* COLRv1BaseAddr() const {
    return reinterpret_cast<const uint8_t*>(mHeader.v1);
  }

  DeviceColor GetColor(uint16_t aPaletteIndex, float aAlpha) const;

  // Convert from FUnits (either integer or Fixed 16.16) to device pixels.
  template <typename T>
  float F2P(T aPixels) const {
    return mFontUnitsToPixels * float(aPixels);
  }
};

constexpr uint32_t kPaintRecursionLimit = 256;

DeviceColor PaintState::GetColor(uint16_t aPaletteIndex, float aAlpha) const {
  sRGBColor color;
  if (aPaletteIndex < mNumColors) {
    hb_color_t c = mPalette[uint16_t(aPaletteIndex)];
    // Explicitly get the components from the hb_color_t, rather than assuming
    // anything about its packing.
    color = sRGBColor::FromU8(hb_color_get_red(c), hb_color_get_green(c),
                              hb_color_get_blue(c), hb_color_get_alpha(c));
  } else if (aPaletteIndex == 0xffff) {
    color = mCurrentColor;
    if (mUsedCurrentColor) {
      *mUsedCurrentColor = true;
    }
  } else {
    // Palette index out-of-range (a font bug), or palette failed to load.
    // Return partially-opaque gray, so shapes are at least somewhat visible.
    color = sRGBColor(0, 0, 0, 0.25);
  }
  color.a *= aAlpha;
  return ToDeviceColor(color);
}

static bool DispatchPaint(const PaintState& aState, uint32_t aDepth,
                          uint32_t aOffset,
                          const Rect* aBounds /* may be nullptr if unknown */);
static UniquePtr<Pattern> DispatchMakePattern(const PaintState& aState,
                                              uint32_t aOffset);
static Rect DispatchGetBounds(const PaintState& aState, uint32_t aDepth,
                              uint32_t aOffset);
static Matrix DispatchGetMatrix(const PaintState& aState, uint32_t aOffset,
                                uint32_t* aWrappedPaint = nullptr);

// Variation-data types
struct Fixed {
  enum { kFractionBits = 16 };
  operator float() const {
    return float(int32_t(value)) / float(1 << kFractionBits);
  }
  int32_t intRepr() const { return int32_t(value); }

 private:
  int32 value;
};

struct F2DOT14 {
  enum { kFractionBits = 14 };
  operator float() const {
    return float(int16_t(value)) / float(1 << kFractionBits);
  }
  int32_t intRepr() const { return int16_t(value); }

 private:
  int16 value;
};

// Saturating addition used for variation indexes to avoid wrap-around.
static uint32_t SatAdd(uint32_t a, uint32_t b) {
  if (a <= std::numeric_limits<uint32_t>::max() - b) {
    return a + b;
  }
  return std::numeric_limits<uint32_t>::max();
}

struct RegionAxisCoordinates {
  F2DOT14 startCoord;
  F2DOT14 peakCoord;
  F2DOT14 endCoord;
};

struct VariationRegion {
  // RegionAxisCoordinates regionAxes[axisCount];
  const RegionAxisCoordinates* regionAxes() const {
    return reinterpret_cast<const RegionAxisCoordinates*>(this);
  }
};

struct VariationRegionList {
  uint16 axisCount;
  uint16 regionCount;
  // VariationRegion variationRegions[regionCount];
  const char* variationRegionsBase() const {
    return reinterpret_cast<const char*>(this + 1);
  }
  size_t regionSize() const {
    return uint16_t(axisCount) * sizeof(RegionAxisCoordinates);
  }
  const VariationRegion* getRegion(uint16_t i) const {
    if (i >= uint16_t(regionCount)) {
      return nullptr;
    }
    return reinterpret_cast<const VariationRegion*>(variationRegionsBase() +
                                                    i * regionSize());
  }
  bool Validate(const COLRv1Header* aHeader, uint64_t aLength) const {
    if (variationRegionsBase() - reinterpret_cast<const char*>(aHeader) +
            uint16_t(regionCount) * regionSize() >
        aLength) {
      return false;
    }
    return true;
  }
};

struct DeltaSet {
  // (int16 and int8)
  // *or*
  // (int32 and int16) deltaData[regionIndexCount];
};

struct DeltaSetIndexMap {
  enum { INNER_INDEX_BIT_COUNT_MASK = 0x0f, MAP_ENTRY_SIZE_MASK = 0x30 };
  uint8_t format;
  uint8_t entryFormat;
  union {
    struct {
      uint16 mapCount;
      // uint8 mapData[variable];
    } v0;
    struct {
      uint32 mapCount;
      // uint8 mapData[variable];
    } v1;
  };
  uint32_t entrySize() const {
    return (((entryFormat & MAP_ENTRY_SIZE_MASK) >> 4) + 1);
  }
  uint32_t map(uint32_t aIndex) const {
    uint32_t mapCount;
    const uint8_t* mapData;
    switch (format) {
      case 0:
        mapCount = uint32_t(v0.mapCount);
        mapData = reinterpret_cast<const uint8_t*>(&v0.mapCount) +
                  sizeof(v0.mapCount);
        break;
      case 1:
        mapCount = uint32_t(v1.mapCount);
        mapData = reinterpret_cast<const uint8_t*>(&v1.mapCount) +
                  sizeof(v1.mapCount);
        break;
      default:
        // unknown DeltaSetIndexMap format
        return aIndex;
    }
    if (!mapCount) {
      return aIndex;
    }
    if (aIndex >= mapCount) {
      aIndex = mapCount - 1;
    }
    const uint8_t* entryData = mapData + aIndex * entrySize();
    uint32_t entry = 0;
    for (uint32_t i = 0; i < entrySize(); ++i) {
      entry = (entry << 8) + entryData[i];
    }
    uint16_t outerIndex =
        entry >> ((entryFormat & INNER_INDEX_BIT_COUNT_MASK) + 1);
    uint16_t innerIndex =
        entry & ((1 << ((entryFormat & INNER_INDEX_BIT_COUNT_MASK) + 1)) - 1);
    return (uint32_t(outerIndex) << 16) + innerIndex;
  }
  bool Validate(const COLRv1Header* aHeader, uint64_t aLength) const;
};

enum EntryFormatMasks {
  INNER_INDEX_BIT_COUNT_MASK = 0x0f,
  MAP_ENTRY_SIZE_MASK = 0x30
};

struct ItemVariationData {
  enum { WORD_DELTA_COUNT_MASK = 0x7FFF, LONG_WORDS = 0x8000 };
  uint16 itemCount;
  uint16 wordDeltaCount;
  uint16 regionIndexCount;
  // uint16 regionIndexes[regionIndexCount];
  const uint16* regionIndexes() const {
    return reinterpret_cast<const uint16*>(
        reinterpret_cast<const char*>(this + 1));
  }
  // DeltaSet deltaSets[itemCount];
  const DeltaSet* deltaSets() const {
    return reinterpret_cast<const DeltaSet*>(
        reinterpret_cast<const char*>(this + 1) +
        uint16_t(regionIndexCount) * sizeof(uint16));
  }
  bool Validate(const COLRv1Header* aHeader, uint64_t aLength) const;
};

struct ItemVariationStore {
  uint16 format;
  Offset32 variationRegionListOffset;
  uint16 itemVariationDataCount;
  // Offset32 itemVariationDataOffsets[itemVariationDataCount];
  const Offset32* itemVariationDataOffsets() const {
    return reinterpret_cast<const Offset32*>(
        reinterpret_cast<const char*>(this + 1));
  }
  const VariationRegionList* variationRegionList() const {
    return reinterpret_cast<const VariationRegionList*>(
        reinterpret_cast<const char*>(this) + variationRegionListOffset);
  }
  bool Validate(const COLRv1Header* aHeader, uint64_t aLength) const;
};

static int32_t ApplyVariation(const PaintState& aState, int32_t aValue,
                              uint32_t aIndex) {
  if (aIndex == 0xffffffff) {
    return aValue;
  }
  const auto* store = aState.mHeader.v1->itemVariationStore();
  if (!store || uint16_t(store->format) != 1) {
    return aValue;
  }
  const DeltaSetIndexMap* map = aState.mHeader.v1->varIndexMap();
  uint32_t mappedIndex = map ? map->map(aIndex) : aIndex;
  uint16_t outerIndex = mappedIndex >> 16;
  uint16_t innerIndex = mappedIndex & 0xffff;
  const auto* itemVariationDataOffsets = store->itemVariationDataOffsets();
  if (mappedIndex == 0xffffffff ||
      outerIndex >= uint16_t(store->itemVariationDataCount) ||
      !itemVariationDataOffsets[outerIndex]) {
    return aValue;
  }
  const auto* regionList = store->variationRegionList();
  if (outerIndex >= uint16_t(store->itemVariationDataCount)) {
    return aValue;
  }
  const auto* variationData = reinterpret_cast<const ItemVariationData*>(
      reinterpret_cast<const char*>(store) +
      itemVariationDataOffsets[outerIndex]);
  if (innerIndex >= uint16_t(variationData->itemCount)) {
    return aValue;
  }
  const auto* regionIndexes = variationData->regionIndexes();
  uint16_t regionIndexCount = variationData->regionIndexCount;
  const DeltaSet* deltaSets = variationData->deltaSets();
  uint16_t wordDeltaCount = variationData->wordDeltaCount;
  bool longWords = wordDeltaCount & ItemVariationData::LONG_WORDS;
  wordDeltaCount &= ItemVariationData::WORD_DELTA_COUNT_MASK;
  uint32_t deltaSetSize = (regionIndexCount + wordDeltaCount) << longWords;
  const uint8_t* deltaData =
      reinterpret_cast<const uint8_t*>(deltaSets) + deltaSetSize * innerIndex;
  if (deltaData < reinterpret_cast<const uint8_t*>(deltaSets) ||
      deltaData > aState.COLRv1BaseAddr() + aState.mCOLRLength - deltaSetSize) {
    return aValue;
  }
  uint16_t deltaSize = longWords ? 4 : 2;
  int32_t result = aValue;
  for (uint16_t i = 0; i < regionIndexCount; ++i, deltaData += deltaSize) {
    if (i == wordDeltaCount) {
      deltaSize >>= 1;
    }
    const auto* region = regionList->getRegion(uint16_t(regionIndexes[i]));
    if (!region) {
      return aValue;
    }
    // XXX Should we do the calculations here in fixed-point? Check spec.
    float scalar = -1.0;
    for (uint16_t axisIndex = 0; axisIndex < uint16_t(regionList->axisCount);
         ++axisIndex) {
      const auto& axis = region->regionAxes()[axisIndex];
      float peak = axis.peakCoord;
      if (peak == 0.0) {
        // This axis cannot contribute to scalar.
        continue;
      }
      float start = axis.startCoord;
      float end = axis.endCoord;
      float value = axisIndex < aState.mCoordCount
                        ? float(aState.mCoords[axisIndex]) / 16384.0f
                        : 0.0;
      if (value < start || value > end) {
        // Out of range: this region is not applicable.
        scalar = -1.0;
        break;
      }
      if (scalar < 0.0) {
        scalar = 1.0;
      }
      if (value == peak) {
        continue;
      }
      if (value < peak && peak > start) {
        scalar *= (value - start) / (peak - start);
      } else if (value > peak && peak < end) {
        scalar *= (end - value) / (end - peak);
      }
    }
    if (scalar <= 0.0) {
      continue;
    }
    int32_t delta = *reinterpret_cast<const int8_t*>(deltaData);  // sign-extend
    for (uint16_t j = 1; j < deltaSize; ++j) {
      delta = (delta << 8) | deltaData[j];
    }
    delta = int32_t(floorf((float(delta) * scalar) + 0.5f));
    result += delta;
  }
  return result;
};

static float ApplyVariation(const PaintState& aState, Fixed aValue,
                            uint32_t aIndex) {
  return float(ApplyVariation(aState, aValue.intRepr(), aIndex)) /
         (1 << Fixed::kFractionBits);
}

static float ApplyVariation(const PaintState& aState, F2DOT14 aValue,
                            uint32_t aIndex) {
  return float(ApplyVariation(aState, aValue.intRepr(), aIndex)) /
         (1 << F2DOT14::kFractionBits);
}

struct ClipBoxFormat1 {
  enum { kFormat = 1 };
  uint8_t format;
  FWORD xMin;
  FWORD yMin;
  FWORD xMax;
  FWORD yMax;

  Rect GetRect(const PaintState& aState) const {
    MOZ_ASSERT(format == kFormat);
    int32_t x0 = int16_t(xMin);
    int32_t y0 = int16_t(yMin);
    int32_t x1 = int16_t(xMax);
    int32_t y1 = int16_t(yMax);
    // Flip the y-coordinates to map from OpenType to Moz2d space.
    return Rect(aState.F2P(x0), -aState.F2P(y1), aState.F2P(x1 - x0),
                aState.F2P(y1 - y0));
  }
};

struct ClipBoxFormat2 : public ClipBoxFormat1 {
  enum { kFormat = 2 };
  uint32 varIndexBase;

  Rect GetRect(const PaintState& aState) const {
    MOZ_ASSERT(format == kFormat);
    int32_t x0 = ApplyVariation(aState, int16_t(xMin), varIndexBase);
    int32_t y0 = ApplyVariation(aState, int16_t(yMin), SatAdd(varIndexBase, 1));
    int32_t x1 = ApplyVariation(aState, int16_t(xMax), SatAdd(varIndexBase, 2));
    int32_t y1 = ApplyVariation(aState, int16_t(yMax), SatAdd(varIndexBase, 3));
    return Rect(aState.F2P(x0), -aState.F2P(y1), aState.F2P(x1 - x0),
                aState.F2P(y1 - y0));
  }
};

struct Clip {
  uint16 startGlyphID;
  uint16 endGlyphID;
  Offset24 clipBoxOffset;

  Rect GetRect(const PaintState& aState) const {
    uint32_t offset = aState.mHeader.v1->clipListOffset + clipBoxOffset;
    const auto* box = aState.COLRv1BaseAddr() + offset;
    switch (*box) {
      case 1:
        return reinterpret_cast<const ClipBoxFormat1*>(box)->GetRect(aState);
      case 2:
        return reinterpret_cast<const ClipBoxFormat2*>(box)->GetRect(aState);
      default:
        // unknown ClipBoxFormat
        break;
    }
    return Rect();
  }
  bool Validate(const COLRv1Header* aHeader, uint64_t aLength) const;
};

struct ClipList {
  uint8_t format;
  uint32 numClips;
  // Clip clips[numClips]
  const Clip* clips() const { return reinterpret_cast<const Clip*>(this + 1); }
  const Clip* GetClip(uint32_t aGlyphId) const {
    auto compare = [](const void* key, const void* data) -> int {
      uint32_t glyphId = (uint32_t)(uintptr_t)key;
      const auto* clip = reinterpret_cast<const Clip*>(data);
      uint32_t start = uint16_t(clip->startGlyphID);
      uint32_t end = uint16_t(clip->endGlyphID);
      if (start <= glyphId && end >= glyphId) {
        return 0;
      }
      return start > glyphId ? -1 : 1;
    };
    return reinterpret_cast<const Clip*>(bsearch((void*)(uintptr_t)aGlyphId,
                                                 clips(), uint32_t(numClips),
                                                 sizeof(Clip), compare));
  }
  bool Validate(const COLRv1Header* aHeader, uint64_t aLength) const;
};

struct LayerRecord {
  uint16 glyphId;
  uint16 paletteEntryIndex;

  bool Paint(const PaintState& aState, float aAlpha,
             const Point& aPoint) const {
    Glyph glyph{uint16_t(glyphId), aPoint};
    GlyphBuffer buffer{&glyph, 1};
    aState.mDrawTarget->FillGlyphs(
        aState.mScaledFont, buffer,
        ColorPattern(aState.GetColor(paletteEntryIndex, aAlpha)),
        aState.mDrawOptions);
    return true;
  }
};

struct BaseGlyphRecord {
  uint16 glyphId;
  uint16 firstLayerIndex;
  uint16 numLayers;

  bool Paint(const PaintState& aState, float aAlpha,
             const Point& aPoint) const {
    uint32_t layerIndex = uint16_t(firstLayerIndex);
    uint32_t end = layerIndex + uint16_t(numLayers);
    if (end > uint16_t(aState.mHeader.v0->numLayerRecords)) {
      MOZ_ASSERT_UNREACHABLE("bad COLRv0 table");
      return false;
    }
    const auto* layers = aState.mHeader.v0->GetLayerRecords();
    while (layerIndex < end) {
      if (!layers[layerIndex].Paint(aState, aAlpha, aPoint)) {
        return false;
      }
      ++layerIndex;
    }
    return true;
  }
};

struct ColorStop {
  F2DOT14 stopOffset;
  uint16 paletteIndex;
  F2DOT14 alpha;

  float GetStopOffset(const PaintState& aState) const { return stopOffset; }
  uint16_t GetPaletteIndex() const { return paletteIndex; }
  float GetAlpha(const PaintState& aState) const { return alpha; }
};

struct VarColorStop : public ColorStop {
  uint32 varIndexBase;

  float GetStopOffset(const PaintState& aState) const {
    return ApplyVariation(aState, stopOffset, varIndexBase);
  }
  float GetAlpha(const PaintState& aState) const {
    return ApplyVariation(aState, alpha, SatAdd(varIndexBase, 1));
  }
};

template <typename T>
struct ColorLineT {
  enum { EXTEND_PAD = 0, EXTEND_REPEAT = 1, EXTEND_REFLECT = 2 };
  uint8_t extend;
  uint16 numStops;
  const T* colorStops() const { return reinterpret_cast<const T*>(this + 1); }

  // If the color line has only one stop, return it as a simple ColorPattern.
  UniquePtr<Pattern> AsSolidColor(const PaintState& aState) const {
    if (uint16_t(numStops) != 1) {
      return nullptr;
    }
    const auto* stop = colorStops();
    return std::make_unique<ColorPattern>(
        aState.GetColor(stop->GetPaletteIndex(), stop->GetAlpha(aState)));
  }

  // Retrieve the color stops into an array of GradientStop records. The stops
  // are normalized to the range [0 .. 1], and the original offsets of the
  // first and last stops are returned.
  // If aReverse is true, the color line is reversed.
  void CollectGradientStops(const PaintState& aState,
                            nsTArray<GradientStop>& aStops, float* aFirstStop,
                            float* aLastStop, bool aReverse = false) const {
    MOZ_ASSERT(aStops.IsEmpty());
    uint16_t count = numStops;
    if (!count) {
      return;
    }
    const auto* stop = colorStops();
    if (reinterpret_cast<const uint8_t*>(stop) + count * sizeof(T) >
        aState.COLRv1BaseAddr() + aState.mCOLRLength) {
      return;
    }
    aStops.SetCapacity(count);
    for (uint16_t i = 0; i < count; ++i, ++stop) {
      DeviceColor color =
          aState.GetColor(stop->GetPaletteIndex(), stop->GetAlpha(aState));
      aStops.AppendElement(GradientStop{stop->GetStopOffset(aState), color});
    }
    if (count == 1) {
      *aFirstStop = *aLastStop = aStops[0].offset;
      return;
    }
    aStops.StableSort();
    if (aReverse) {
      float a = aStops[0].offset;
      float b = aStops.LastElement().offset;
      aStops.Reverse();
      for (auto& gs : aStops) {
        gs.offset = a + b - gs.offset;
      }
    }
    // Normalize stops to the range 0.0 .. 1.0, and return the original
    // start & end.
    // Note that if all stops are at the same offset, no normalization
    // will be done.
    *aFirstStop = aStops[0].offset;
    *aLastStop = aStops.LastElement().offset;
    if ((*aLastStop > *aFirstStop) &&
        (*aLastStop != 1.0f || *aFirstStop != 0.0f)) {
      float f = 1.0f / (*aLastStop - *aFirstStop);
      for (auto& gs : aStops) {
        gs.offset = (gs.offset - *aFirstStop) * f;
      }
    }
  }

  // Create a gfx::GradientStops representing the given color line stops,
  // applying our extend mode.
  already_AddRefed<GradientStops> MakeGradientStops(
      const PaintState& aState, nsTArray<GradientStop>& aStops) const {
    auto mapExtendMode = [](uint8_t aExtend) -> ExtendMode {
      switch (aExtend) {
        case EXTEND_REPEAT:
          return ExtendMode::REPEAT;
        case EXTEND_REFLECT:
          return ExtendMode::REFLECT;
        case EXTEND_PAD:
        default:
          return ExtendMode::CLAMP;
      }
    };
    return aState.mDrawTarget->CreateGradientStops(
        aStops.Elements(), aStops.Length(), mapExtendMode(extend));
  }

  already_AddRefed<GradientStops> MakeGradientStops(
      const PaintState& aState, float* aFirstStop, float* aLastStop,
      bool aReverse = false) const {
    AutoTArray<GradientStop, 8> stops;
    CollectGradientStops(aState, stops, aFirstStop, aLastStop, aReverse);
    if (stops.IsEmpty()) {
      return nullptr;
    }
    return MakeGradientStops(aState, stops);
  }
};

using ColorLine = ColorLineT<ColorStop>;
using VarColorLine = ColorLineT<VarColorStop>;

// Used to check for cycles in the paint graph, and bail out to avoid infinite
// recursion when traversing the graph in Paint() or GetBoundingRect(). (Only
// PaintColrLayers and PaintColrGlyph can cause cycles; all other paint types
// have only forward references within the table.)
#define IF_CYCLE_RETURN(retval)             \
  if (aState.mVisited->Contains(aOffset)) { \
    return retval;                          \
  }                                         \
  aState.mVisited->AppendElement(aOffset);  \
  ScopeExit e([&aState]() { aState.mVisited->RemoveLastElement(); })

struct PaintColrLayers {
  enum { kFormat = 1 };
  uint8_t format;
  uint8_t numLayers;
  uint32 firstLayerIndex;

  bool Paint(const PaintState& aState, uint32_t aDepth, uint32_t aOffset,
             const Rect* aBounds) const {
    MOZ_ASSERT(format == kFormat);
    IF_CYCLE_RETURN(true);
    const auto* layerList = aState.mHeader.v1->layerList();
    if (!layerList) {
      return false;
    }
    if (uint64_t(firstLayerIndex) + numLayers > layerList->numLayers) {
      return false;
    }
    const auto* paintOffsets = layerList->paintOffsets() + firstLayerIndex;
    for (uint32_t i = 0; i < numLayers; i++) {
      if (!DispatchPaint(aState, aDepth,
                         aState.mHeader.v1->layerListOffset + paintOffsets[i],
                         aBounds)) {
        return false;
      }
    }
    return true;
  }

  Rect GetBoundingRect(const PaintState& aState, uint32_t aDepth,
                       uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    IF_CYCLE_RETURN(Rect());
    const auto* layerList = aState.mHeader.v1->layerList();
    if (!layerList) {
      return Rect();
    }
    if (uint64_t(firstLayerIndex) + numLayers > layerList->numLayers) {
      return Rect();
    }
    Rect result;
    const auto* paintOffsets = layerList->paintOffsets() + firstLayerIndex;
    for (uint32_t i = 0; i < numLayers; i++) {
      result = result.Union(DispatchGetBounds(
          aState, aDepth,
          aState.mHeader.v1->layerListOffset + paintOffsets[i]));
    }
    return result;
  }
};

struct PaintPatternBase {
  bool Paint(const PaintState& aState, uint32_t aDepth, uint32_t aOffset,
             const Rect* aBounds) const {
    Matrix m = aState.mDrawTarget->GetTransform();
    if (m.Invert()) {
      if (auto pattern = DispatchMakePattern(aState, aOffset)) {
        aState.mDrawTarget->FillRect(
            m.TransformBounds(IntRectToRect(aState.mDrawTarget->GetRect())),
            *pattern, aState.mDrawOptions);
        return true;
      }
    }
    return false;
  }

  Rect GetBoundingRect(const PaintState& aState, uint32_t aDepth,
                       uint32_t aOffset) const {
    return Rect();
  }
};

struct PaintSolid : public PaintPatternBase {
  enum { kFormat = 2 };
  uint8_t format;
  uint16 paletteIndex;
  F2DOT14 alpha;

  UniquePtr<Pattern> MakePattern(const PaintState& aState,
                                 uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    return std::make_unique<ColorPattern>(aState.GetColor(paletteIndex, alpha));
  }
};

struct PaintVarSolid : public PaintSolid {
  enum { kFormat = 3 };
  uint32 varIndexBase;

  UniquePtr<Pattern> MakePattern(const PaintState& aState,
                                 uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    return std::make_unique<ColorPattern>(aState.GetColor(
        paletteIndex, ApplyVariation(aState, alpha, varIndexBase)));
  }
};

struct PaintLinearGradient : public PaintPatternBase {
  enum { kFormat = 4 };
  uint8_t format;
  Offset24 colorLineOffset;
  FWORD x0;
  FWORD y0;
  FWORD x1;
  FWORD y1;
  FWORD x2;
  FWORD y2;

  UniquePtr<Pattern> MakePattern(const PaintState& aState,
                                 uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    uint32_t clOffset = aOffset + colorLineOffset;
    if (clOffset + sizeof(ColorLine) + sizeof(ColorStop) > aState.mCOLRLength) {
      return nullptr;
    }
    const auto* colorLine =
        reinterpret_cast<const ColorLine*>(aState.COLRv1BaseAddr() + clOffset);
    Point p0(aState.F2P(int16_t(x0)), aState.F2P(int16_t(y0)));
    Point p1(aState.F2P(int16_t(x1)), aState.F2P(int16_t(y1)));
    Point p2(aState.F2P(int16_t(x2)), aState.F2P(int16_t(y2)));
    return NormalizeAndMakeGradient(aState, colorLine, p0, p1, p2);
  }

  template <typename T>
  UniquePtr<Pattern> NormalizeAndMakeGradient(const PaintState& aState,
                                              const T* aColorLine, Point p0,
                                              Point p1, Point p2) const {
    // Ill-formed gradient should not be rendered.
    if (p1 == p0 || p2 == p0) {
      return std::make_unique<ColorPattern>(DeviceColor());
    }
    UniquePtr<Pattern> solidColor = aColorLine->AsSolidColor(aState);
    if (solidColor) {
      return solidColor;
    }
    float firstStop, lastStop;
    AutoTArray<GradientStop, 8> stopArray;
    aColorLine->CollectGradientStops(aState, stopArray, &firstStop, &lastStop);
    if (stopArray.IsEmpty()) {
      return std::make_unique<ColorPattern>(DeviceColor());
    }
    if (firstStop != 0.0 || lastStop != 1.0) {
      if (firstStop == lastStop) {
        if (aColorLine->extend != T::EXTEND_PAD) {
          return std::make_unique<ColorPattern>(DeviceColor());
        }
        // For extend-pad, when the color line is zero-length, we add a "fake"
        // color stop to create a [0.0..1.0]-normalized color line, so that the
        // projection of points below works as expected.
        for (auto& gs : stopArray) {
          gs.offset = 0.0f;
        }
        stopArray.AppendElement(
            GradientStop{1.0f, stopArray.LastElement().color});
        lastStop += 1.0f;
      }
      // Adjust positions of the points to account for normalization of the
      // color line stop offsets.
      Point v = p1 - p0;
      p0 += v * firstStop;
      p1 -= v * (1.0f - lastStop);
      // Move the rotation vector to maintain the same direction from p0.
      p2 += v * firstStop;
    }
    Point p3;
    if (FuzzyEqualsMultiplicative(p2.y, p0.y)) {
      // rotation vector is horizontal
      p3 = Point(p0.x, p1.y);
    } else if (FuzzyEqualsMultiplicative(p2.x, p0.x)) {
      // rotation vector is vertical
      p3 = Point(p1.x, p0.y);
    } else {
      float m = (p2.y - p0.y) / (p2.x - p0.x);  // slope of line p0->p2
      float mInv = -1.0f / m;         // slope of desired perpendicular p0->p3
      float c1 = p0.y - mInv * p0.x;  // line p0->p3 is m * x - y + c1 = 0
      float c2 = p1.y - m * p1.x;     // line p1->p3 is mInv * x - y + c2 = 0
      float x3 = (c1 - c2) / (m - mInv);
      float y3 = (c1 * m - c2 * mInv) / (m - mInv);
      p3 = Point(x3, y3);
    }
    RefPtr stops = aColorLine->MakeGradientStops(aState, stopArray);
    return std::make_unique<LinearGradientPattern>(p0, p3, std::move(stops),
                                                   Matrix::Scaling(1.0, -1.0));
  }
};

struct PaintVarLinearGradient : public PaintLinearGradient {
  enum { kFormat = 5 };
  uint32 varIndexBase;

  UniquePtr<Pattern> MakePattern(const PaintState& aState,
                                 uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    uint32_t clOffset = aOffset + colorLineOffset;
    if (clOffset + sizeof(VarColorLine) + sizeof(VarColorStop) >
        aState.mCOLRLength) {
      return nullptr;
    }
    const auto* colorLine = reinterpret_cast<const VarColorLine*>(
        aState.COLRv1BaseAddr() + clOffset);
    Point p0(aState.F2P(ApplyVariation(aState, int16_t(x0), varIndexBase)),
             aState.F2P(
                 ApplyVariation(aState, int16_t(y0), SatAdd(varIndexBase, 1))));
    Point p1(aState.F2P(
                 ApplyVariation(aState, int16_t(x1), SatAdd(varIndexBase, 2))),
             aState.F2P(
                 ApplyVariation(aState, int16_t(y1), SatAdd(varIndexBase, 3))));
    Point p2(aState.F2P(
                 ApplyVariation(aState, int16_t(x2), SatAdd(varIndexBase, 4))),
             aState.F2P(
                 ApplyVariation(aState, int16_t(y2), SatAdd(varIndexBase, 5))));
    return NormalizeAndMakeGradient(aState, colorLine, p0, p1, p2);
  }
};

struct PaintRadialGradient : public PaintPatternBase {
  enum { kFormat = 6 };
  uint8_t format;
  Offset24 colorLineOffset;
  FWORD x0;
  FWORD y0;
  UFWORD radius0;
  FWORD x1;
  FWORD y1;
  UFWORD radius1;

  UniquePtr<Pattern> MakePattern(const PaintState& aState,
                                 uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    uint32_t clOffset = aOffset + colorLineOffset;
    if (clOffset + sizeof(ColorLine) + sizeof(ColorStop) > aState.mCOLRLength) {
      return nullptr;
    }
    const auto* colorLine =
        reinterpret_cast<const ColorLine*>(aState.COLRv1BaseAddr() + clOffset);
    Point c1(aState.F2P(int16_t(x0)), aState.F2P(int16_t(y0)));
    Point c2(aState.F2P(int16_t(x1)), aState.F2P(int16_t(y1)));
    float r1 = aState.F2P(uint16_t(radius0));
    float r2 = aState.F2P(uint16_t(radius1));
    return NormalizeAndMakeGradient(aState, colorLine, c1, c2, r1, r2);
  }

  // Helper function to trim the gradient stops array at the start or end.
  void TruncateGradientStops(nsTArray<GradientStop>& aStops, float aStart,
                             float aEnd) const {
    // For pad mode, we may need a sub-range of the line: figure out which
    // stops to trim, and interpolate as needed at truncation point(s).
    // (Currently this is only ever used to trim one end of the color line,
    // so edge cases that may occur when trimming both ends are untested.)
    MOZ_ASSERT(aStart == 0.0f || aEnd == 1.0f,
               "Trimming both ends of color-line is untested!");

    // Create a color that is |r| of the way from c1 to c2.
    auto interpolateColor = [](DeviceColor c1, DeviceColor c2, float r) {
      return DeviceColor(
          c2.r * r + c1.r * (1.0f - r), c2.g * r + c1.g * (1.0f - r),
          c2.b * r + c1.b * (1.0f - r), c2.a * r + c1.a * (1.0f - r));
    };

    size_t count = aStops.Length();
    MOZ_ASSERT(count > 1);

    // Truncate at the start of the color line?
    if (aStart > 0.0f) {
      // Skip forward past any stops that can be dropped.
      size_t i = 0;
      while (i < count - 1 && aStops[i].offset < aStart) {
        ++i;
      }
      // If we're not truncating exactly at a color-stop offset, shift the
      // preceding stop to the truncation offset and interpolate its color.
      if (i && aStops[i].offset > aStart) {
        auto& prev = aStops[i - 1];
        auto& curr = aStops[i];
        float ratio = (aStart - prev.offset) / (curr.offset - prev.offset);
        prev.color = interpolateColor(prev.color, curr.color, ratio);
        prev.offset = aStart;
        --i;  // We don't want to remove this stop, as we adjusted it.
      }
      aStops.RemoveElementsAt(0, i);
      // Re-normalize the remaining stops to the [0, 1] range.
      if (aStart < 1.0f) {
        float r = 1.0f / (1.0f - aStart);
        for (auto& gs : aStops) {
          gs.offset = r * (gs.offset - aStart);
        }
      }
    }

    // Truncate at the end of the color line?
    if (aEnd < 1.0f) {
      // Skip back over any stops that can be dropped.
      size_t i = count - 1;
      while (i && aStops[i].offset > aEnd) {
        --i;
      }
      // If we're not truncating exactly at a color-stop offset, shift the
      // following stop to the truncation offset and interpolate its color.
      if (i + 1 < count && aStops[i].offset < aEnd) {
        auto& next = aStops[i + 1];
        auto& curr = aStops[i];
        float ratio = (aEnd - curr.offset) / (next.offset - curr.offset);
        next.color = interpolateColor(curr.color, next.color, ratio);
        next.offset = aEnd;
        ++i;
      }
      aStops.RemoveElementsAt(i + 1, count - i - 1);
      // Re-normalize the remaining stops to the [0, 1] range.
      if (aEnd > 0.0f) {
        float r = 1.0f / aEnd;
        for (auto& gs : aStops) {
          gs.offset = r * gs.offset;
        }
      }
    }
  }

  template <typename T>
  UniquePtr<Pattern> NormalizeAndMakeGradient(const PaintState& aState,
                                              const T* aColorLine, Point c1,
                                              Point c2, float r1,
                                              float r2) const {
    if ((c1 == c2 && r1 == r2) || (r1 == 0.0 && r2 == 0.0)) {
      return std::make_unique<ColorPattern>(DeviceColor());
    }
    UniquePtr<Pattern> solidColor = aColorLine->AsSolidColor(aState);
    if (solidColor) {
      return solidColor;
    }
    float firstStop, lastStop;
    AutoTArray<GradientStop, 8> stopArray;
    aColorLine->CollectGradientStops(aState, stopArray, &firstStop, &lastStop);
    if (stopArray.IsEmpty()) {
      return std::make_unique<ColorPattern>(DeviceColor());
    }
    // If the color stop offsets had to be normalized to the [0, 1] range,
    // adjust the circle positions and radii to match.
    if (firstStop != 0.0f || lastStop != 1.0f) {
      if (firstStop == lastStop) {
        if (aColorLine->extend != T::EXTEND_PAD) {
          return std::make_unique<ColorPattern>(DeviceColor());
        }
        // For extend-pad, when the color line is zero-length, we add a "fake"
        // color stop to ensure we'll maintain the orientation of the cone,
        // otherwise when we adjust circles to account for the normalized color
        // stops, the centers will coalesce and the cone or cylinder collapses.
        for (auto& gs : stopArray) {
          gs.offset = 0.0f;
        }
        stopArray.AppendElement(
            GradientStop{1.0f, stopArray.LastElement().color});
        lastStop += 1.0f;
      }
      // Adjust centers along the vector between them, and scale radii for
      // gradient line defined from 0.0 to 1.0.
      Point vec = c2 - c1;
      c1 += vec * firstStop;
      c2 -= vec * (1.0f - lastStop);
      float deltaR = r2 - r1;
      r1 = r1 + deltaR * firstStop;
      r2 = r2 - deltaR * (1.0f - lastStop);
    }
    if ((r1 < 0.0f || r2 < 0.0f) && aColorLine->extend == T::EXTEND_PAD) {
      // For EXTEND_PAD, we can restrict the gradient definition to just its
      // visible portion because the shader doesn't need to see any part of the
      // color line that extends into the negative-radius "virtual cone".
      if (r1 < 0.0f && r2 < 0.0f) {
        // If both radii are negative, then only the color at the closer circle
        // will appear in the projected positive cone (or if they're equal,
        // nothing will be visible at all).
        if (r1 == r2) {
          return std::make_unique<ColorPattern>(DeviceColor());
        }
        // The defined range of the color line is entirely in the invisible
        // cone; all that will project into visible space is a single color.
        if (r1 < r2) {
          // Keep only the last color stop.
          stopArray.RemoveElementsAt(0, stopArray.Length() - 1);
        } else {
          // Keep only the first color stop.
          stopArray.RemoveElementsAt(1, stopArray.Length() - 1);
        }
      } else {
        // Truncate the gradient at the tip of the visible cone: find the color
        // stops closest to that point and interpolate between them.
        if (r1 < r2) {
          float start = r1 / (r1 - r2);
          TruncateGradientStops(stopArray, start, 1.0f);
          r1 = 0.0f;
          c1 = c1 * (1.0f - start) + c2 * start;
        } else if (r2 < r1) {
          float end = 1.0f - r2 / (r2 - r1);
          TruncateGradientStops(stopArray, 0.0f, end);
          r2 = 0.0f;
          c2 = c1 * (1.0f - end) + c2 * end;
        }
      }
    }
    // Handle negative radii, which the shader won't understand directly, by
    // projecting the circles along the cones such that both radii are positive.
    if (r1 < 0.0f || r2 < 0.0f) {
      float deltaR = r2 - r1;
      // If deltaR is zero, then nothing is visible because the cone has
      // degenerated into a negative-radius cylinder, and does not project
      // into visible space at all.
      if (deltaR == 0.0f) {
        return std::make_unique<ColorPattern>(DeviceColor());
      }
      Point vec = c2 - c1;
      if (aColorLine->extend == T::EXTEND_REFLECT) {
        deltaR *= 2.0f;
        vec = vec * 2.0f;
      }
      if (r2 < r1) {
        vec = -vec;
        deltaR = -deltaR;
      }
      // Number of repeats by which we need to shift.
      float n = std::ceil(std::max(-r1, -r2) / deltaR);
      deltaR *= n;
      r1 += deltaR;
      r2 += deltaR;
      vec = vec * n;
      c1 += vec;
      c2 += vec;
    }
    RefPtr stops = aColorLine->MakeGradientStops(aState, stopArray);
    if (!stops) {
      return std::make_unique<ColorPattern>(DeviceColor());
    }
    return std::make_unique<RadialGradientPattern>(
        c1, c2, r1, r2, std::move(stops), Matrix::Scaling(1.0, -1.0));
  }
};

struct PaintVarRadialGradient : public PaintRadialGradient {
  enum { kFormat = 7 };
  uint32 varIndexBase;

  UniquePtr<Pattern> MakePattern(const PaintState& aState,
                                 uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    uint32_t clOffset = aOffset + colorLineOffset;
    if (clOffset + sizeof(VarColorLine) + sizeof(VarColorStop) >
        aState.mCOLRLength) {
      return nullptr;
    }
    const auto* colorLine = reinterpret_cast<const VarColorLine*>(
        aState.COLRv1BaseAddr() + clOffset);
    Point c1(aState.F2P(ApplyVariation(aState, int16_t(x0), varIndexBase)),
             aState.F2P(
                 ApplyVariation(aState, int16_t(y0), SatAdd(varIndexBase, 1))));
    float r1 = aState.F2P(
        ApplyVariation(aState, uint16_t(radius0), SatAdd(varIndexBase, 2)));
    Point c2(aState.F2P(
                 ApplyVariation(aState, int16_t(x1), SatAdd(varIndexBase, 3))),
             aState.F2P(
                 ApplyVariation(aState, int16_t(y1), SatAdd(varIndexBase, 4))));
    float r2 = aState.F2P(
        ApplyVariation(aState, uint16_t(radius1), SatAdd(varIndexBase, 5)));
    return NormalizeAndMakeGradient(aState, colorLine, c1, c2, r1, r2);
  }
};

struct PaintSweepGradient : public PaintPatternBase {
  enum { kFormat = 8 };
  uint8_t format;
  Offset24 colorLineOffset;
  FWORD centerX;
  FWORD centerY;
  F2DOT14 startAngle;
  F2DOT14 endAngle;

  UniquePtr<Pattern> MakePattern(const PaintState& aState,
                                 uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    uint32_t clOffset = aOffset + colorLineOffset;
    if (clOffset + sizeof(ColorLine) + sizeof(ColorStop) > aState.mCOLRLength) {
      return nullptr;
    }
    const auto* colorLine =
        reinterpret_cast<const ColorLine*>(aState.COLRv1BaseAddr() + clOffset);
    float start = float(startAngle) + 1.0f;
    float end = float(endAngle) + 1.0f;
    Point center(aState.F2P(int16_t(centerX)), aState.F2P(int16_t(centerY)));
    return NormalizeAndMakeGradient(aState, colorLine, center, start, end);
  }

  template <typename T>
  UniquePtr<Pattern> NormalizeAndMakeGradient(const PaintState& aState,
                                              const T* aColorLine,
                                              Point aCenter, float aStart,
                                              float aEnd) const {
    if (aStart == aEnd && aColorLine->extend != T::EXTEND_PAD) {
      return std::make_unique<ColorPattern>(DeviceColor());
    }
    UniquePtr<Pattern> solidColor = aColorLine->AsSolidColor(aState);
    if (solidColor) {
      return solidColor;
    }
    // ConicGradientPattern works counterclockwise. If the gradient is defined
    // clockwise (with aStart greater than aEnd), we'll reverse the color line
    // and swap the start and end angles.
    bool reverse = aEnd < aStart;
    float firstStop, lastStop;
    RefPtr stops =
        aColorLine->MakeGradientStops(aState, &firstStop, &lastStop, reverse);
    if (!stops) {
      return nullptr;
    }
    if (firstStop != 0.0 || lastStop != 1.0) {
      if (firstStop == lastStop) {
        if (aColorLine->extend != T::EXTEND_PAD) {
          return std::make_unique<ColorPattern>(DeviceColor());
        }
      } else {
        float sweep = aEnd - aStart;
        aStart = aStart + sweep * firstStop;
        aEnd = aStart + sweep * (lastStop - firstStop);
      }
    }
    if (reverse) {
      std::swap(aStart, aEnd);
    }
    return std::make_unique<ConicGradientPattern>(
        aCenter, M_PI / 2.0, aStart / 2.0, aEnd / 2.0, std::move(stops),
        Matrix::Scaling(1.0, -1.0));
  }
};

struct PaintVarSweepGradient : public PaintSweepGradient {
  enum { kFormat = 9 };
  uint32 varIndexBase;

  UniquePtr<Pattern> MakePattern(const PaintState& aState,
                                 uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    uint32_t clOffset = aOffset + colorLineOffset;
    if (clOffset + sizeof(VarColorLine) + sizeof(VarColorStop) >
        aState.mCOLRLength) {
      return nullptr;
    }
    const auto* colorLine = reinterpret_cast<const VarColorLine*>(
        aState.COLRv1BaseAddr() + clOffset);
    float start =
        ApplyVariation(aState, startAngle, SatAdd(varIndexBase, 2)) + 1.0f;
    float end =
        ApplyVariation(aState, endAngle, SatAdd(varIndexBase, 3)) + 1.0f;
    Point center(
        aState.F2P(ApplyVariation(aState, int16_t(centerX), varIndexBase)),
        aState.F2P(
            ApplyVariation(aState, int16_t(centerY), SatAdd(varIndexBase, 1))));
    return NormalizeAndMakeGradient(aState, colorLine, center, start, end);
  }
};

// Folds aTransform into the pattern's matrix, so that filling with the pattern
// under an untransformed target gives the same result as filling under
// aTransform. Returns false if that is not possible.
static bool ComposePatternTransform(Pattern* aPattern,
                                    const Matrix& aTransform) {
  // PaintPatternBase::Paint paints nothing under a singular transform; leave
  // that case to it.
  if (aTransform.IsSingular()) {
    return false;
  }
  auto compose = [&](auto* aGradient) {
    aGradient->mMatrix = aGradient->mMatrix * aTransform;
    return true;
  };
  switch (aPattern->GetType()) {
    case PatternType::COLOR:
      return true;
    case PatternType::LINEAR_GRADIENT:
      return compose(static_cast<LinearGradientPattern*>(aPattern));
    case PatternType::RADIAL_GRADIENT:
      return compose(static_cast<RadialGradientPattern*>(aPattern));
    case PatternType::CONIC_GRADIENT:
      return compose(static_cast<ConicGradientPattern*>(aPattern));
    default:
      return false;
  }
}

struct PaintGlyph {
  enum { kFormat = 10 };
  uint8_t format;
  Offset24 paintOffset;
  uint16 glyphID;

  bool Paint(const PaintState& aState, uint32_t aDepth, uint32_t aOffset,
             const Rect* aBounds) const {
    MOZ_ASSERT(format == kFormat);
    if (!paintOffset) {
      return true;
    }
    Glyph g{uint16_t(glyphID), Point()};
    GlyphBuffer buffer{&g, 1};
    // If the paint is a simple fill (rather than a sub-graph of further paint
    // records), we can just use FillGlyphs to render it instead of setting up
    // a clip. Transform records in between are folded into the pattern's
    // matrix, so that a fill wrapped in transforms takes this path too.
    Matrix transform;
    uint32_t patternOffset = aOffset + paintOffset;
    // Terminates because a wrapped paint always lies at a higher offset.
    for (;;) {
      uint32_t wrapped = 0;
      Matrix matrix = DispatchGetMatrix(aState, patternOffset, &wrapped);
      if (!wrapped) {
        break;
      }
      transform = matrix * transform;
      patternOffset = wrapped;
    }

    UniquePtr<Pattern> fillPattern = DispatchMakePattern(aState, patternOffset);
    if (fillPattern && ComposePatternTransform(fillPattern.get(), transform)) {
      // On macOS we can't use FillGlyphs because when we render the glyph,
      // Core Text's own color font support may step in and ignore the
      // pattern. So to avoid this, fill the glyph as a path instead.
#if XP_MACOSX
      RefPtr<Path> path = GetPathForGlyphs(aState, buffer);
      aState.mDrawTarget->Fill(path, *fillPattern, aState.mDrawOptions);
#else
      aState.mDrawTarget->FillGlyphs(aState.mScaledFont, buffer, *fillPattern,
                                     aState.mDrawOptions);
#endif
      return true;
    }
    RefPtr<Path> path = GetPathForGlyphs(aState, buffer);
    aState.mDrawTarget->PushClip(path);
    bool ok = DispatchPaint(aState, aDepth, aOffset + paintOffset, aBounds);
    aState.mDrawTarget->PopClip();
    return ok;
  }

  Rect GetBoundingRect(const PaintState& aState, uint32_t aDepth,
                       uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    Glyph g{uint16_t(glyphID), Point()};
    GlyphBuffer buffer{&g, 1};
    RefPtr<Path> path = GetPathForGlyphs(aState, buffer);
    return path->GetFastBounds();
  }

 private:
  RefPtr<Path> GetPathForGlyphs(const PaintState& aState,
                                const GlyphBuffer& buffer) const {
    if (aState.mDrawTarget->GetBackendType() == BackendType::WEBRENDER_TEXT) {
      RefPtr dt = gfxPlatform::ThreadLocalScreenReferenceDrawTarget();
      return aState.mScaledFont->GetPathForGlyphs(buffer, dt);
    }
    return aState.mScaledFont->GetPathForGlyphs(buffer, aState.mDrawTarget);
  }
};

struct PaintColrGlyph {
  enum { kFormat = 11 };
  uint8_t format;
  uint16 glyphID;

  // Factored out as a helper because this is also used by the top-level
  // PaintGlyphGraph function.
  static bool DoPaint(const PaintState& aState, uint32_t aDepth,
                      const BaseGlyphPaintRecord* aBaseGlyphPaint,
                      uint32_t aGlyphId, const Rect* aBounds) {
    AutoPopClips clips(aState.mDrawTarget);
    Rect clipRect;
    if (const auto* clipList = aState.mHeader.v1->clipList()) {
      if (const auto* clip = clipList->GetClip(aGlyphId)) {
        clipRect = clip->GetRect(aState);
        clips.PushClipRect(clipRect);
        if (!aBounds) {
          aBounds = &clipRect;
        }
      }
    }
    return DispatchPaint(
        aState, aDepth,
        aState.mHeader.v1->baseGlyphListOffset + aBaseGlyphPaint->paintOffset,
        aBounds);
  }

  bool Paint(const PaintState& aState, uint32_t aDepth, uint32_t aOffset,
             const Rect* aBounds) const {
    MOZ_ASSERT(format == kFormat);
    IF_CYCLE_RETURN(true);
    const auto* base = aState.mHeader.v1->GetBaseGlyphPaint(glyphID);
    return base ? DoPaint(aState, aDepth, base, uint16_t(glyphID), aBounds)
                : false;
  }

  Rect GetBoundingRect(const PaintState& aState, uint32_t aDepth,
                       uint32_t aOffset) const {
    IF_CYCLE_RETURN(Rect());
    if (const auto* clipList = aState.mHeader.v1->clipList()) {
      if (const auto* clip = clipList->GetClip(uint16_t(glyphID))) {
        return clip->GetRect(aState);
      }
    }
    if (const auto* base =
            aState.mHeader.v1->GetBaseGlyphPaint(uint16_t(glyphID))) {
      return DispatchGetBounds(
          aState, aDepth,
          aState.mHeader.v1->baseGlyphListOffset + base->paintOffset);
    }
    return Rect();
  }
};

#undef IF_CYCLE_RETURN

struct Affine2x3 {
  Fixed xx;
  Fixed yx;
  Fixed xy;
  Fixed yy;
  Fixed dx;
  Fixed dy;

  Matrix AsMatrix(const PaintState& aState) const {
    // Flip signs because of opposite y-axis direction in moz2d vs opentype.
    return Matrix(float(xx), -float(yx), -float(xy), float(yy),
                  aState.F2P(float(dx)), -aState.F2P(float(dy)));
  }
};

struct VarAffine2x3 : public Affine2x3 {
  uint32 varIndexBase;

  Matrix AsMatrix(const PaintState& aState) const {
    return Matrix(
        ApplyVariation(aState, xx, varIndexBase),
        -ApplyVariation(aState, yx, SatAdd(varIndexBase, 1)),
        -ApplyVariation(aState, xy, SatAdd(varIndexBase, 2)),
        ApplyVariation(aState, yy, SatAdd(varIndexBase, 3)),
        aState.F2P(ApplyVariation(aState, dx, SatAdd(varIndexBase, 4))),
        -aState.F2P(ApplyVariation(aState, dy, SatAdd(varIndexBase, 5))));
  };
};

struct PaintTransformBase {
  uint8_t format;
  Offset24 paintOffset;

  bool Paint(const PaintState& aState, uint32_t aDepth, uint32_t aOffset,
             const Rect* aBounds) const {
    if (!paintOffset) {
      return true;
    }
    AutoRestoreTransform saveTransform(aState.mDrawTarget);
    aState.mDrawTarget->ConcatTransform(DispatchGetMatrix(aState, aOffset));
    return DispatchPaint(aState, aDepth, aOffset + paintOffset, aBounds);
  }

  Rect GetBoundingRect(const PaintState& aState, uint32_t aDepth,
                       uint32_t aOffset) const {
    if (!paintOffset) {
      return Rect();
    }
    Rect bounds = DispatchGetBounds(aState, aDepth, aOffset + paintOffset);
    bounds = DispatchGetMatrix(aState, aOffset).TransformBounds(bounds);
    return bounds;
  }
};

struct PaintTransform : public PaintTransformBase {
  enum { kFormat = 12 };
  Offset24 transformOffset;

  Matrix GetMatrix(const PaintState& aState, uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    if (aOffset + transformOffset + sizeof(Affine2x3) > aState.mCOLRLength) {
      return Matrix();
    }
    const auto* t = reinterpret_cast<const Affine2x3*>(
        aState.COLRv1BaseAddr() + aOffset + transformOffset);
    return t->AsMatrix(aState);
  }
};

struct PaintVarTransform : public PaintTransformBase {
  enum { kFormat = 13 };
  Offset24 transformOffset;

  Matrix GetMatrix(const PaintState& aState, uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    if (aOffset + transformOffset + sizeof(VarAffine2x3) > aState.mCOLRLength) {
      return Matrix();
    }
    const auto* t = reinterpret_cast<const VarAffine2x3*>(
        aState.COLRv1BaseAddr() + aOffset + transformOffset);
    return t->AsMatrix(aState);
  }
};

struct PaintTranslate : public PaintTransformBase {
  enum { kFormat = 14 };
  FWORD dx;
  FWORD dy;

  Matrix GetMatrix(const PaintState& aState, uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    return Matrix::Translation(aState.F2P(int16_t(dx)),
                               -aState.F2P(int16_t(dy)));
  }
};

struct PaintVarTranslate : public PaintTranslate {
  enum { kFormat = 15 };
  uint32 varIndexBase;

  Matrix GetMatrix(const PaintState& aState, uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    return Matrix::Translation(
        aState.F2P(ApplyVariation(aState, int16_t(dx), varIndexBase)),
        -aState.F2P(
            ApplyVariation(aState, int16_t(dy), SatAdd(varIndexBase, 1))));
  }
};

struct PaintScale : public PaintTransformBase {
  enum { kFormat = 16 };
  F2DOT14 scaleX;
  F2DOT14 scaleY;

  Matrix GetMatrix(const PaintState& aState, uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    return Matrix::Scaling(float(scaleX), float(scaleY));
  }
};

struct PaintVarScale : public PaintScale {
  enum { kFormat = 17 };
  uint32 varIndexBase;

  Matrix GetMatrix(const PaintState& aState, uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    return Matrix::Scaling(
        ApplyVariation(aState, scaleX, varIndexBase),
        ApplyVariation(aState, scaleY, SatAdd(varIndexBase, 1)));
  }
};

struct PaintScaleAroundCenter : public PaintTransformBase {
  enum { kFormat = 18 };
  F2DOT14 scaleX;
  F2DOT14 scaleY;
  FWORD centerX;
  FWORD centerY;

  Matrix GetMatrix(const PaintState& aState, uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    Point center(aState.F2P(int16_t(centerX)), -aState.F2P(int16_t(centerY)));
    return Matrix::Translation(center)
        .PreScale(float(scaleX), float(scaleY))
        .PreTranslate(-center);
  }
};

struct PaintVarScaleAroundCenter : public PaintScaleAroundCenter {
  enum { kFormat = 19 };
  uint32 varIndexBase;

  Matrix GetMatrix(const PaintState& aState, uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    Point center(aState.F2P(ApplyVariation(aState, int16_t(centerX),
                                           SatAdd(varIndexBase, 2))),
                 -aState.F2P(ApplyVariation(aState, int16_t(centerY),
                                            SatAdd(varIndexBase, 3))));
    return Matrix::Translation(center)
        .PreScale(ApplyVariation(aState, scaleX, varIndexBase),
                  ApplyVariation(aState, scaleY, SatAdd(varIndexBase, 1)))
        .PreTranslate(-center);
  }
};

struct PaintScaleUniform : public PaintTransformBase {
  enum { kFormat = 20 };
  F2DOT14 scale;

  Matrix GetMatrix(const PaintState& aState, uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    return Matrix::Scaling(float(scale), float(scale));
  }
};

struct PaintVarScaleUniform : public PaintScaleUniform {
  enum { kFormat = 21 };
  uint32 varIndexBase;

  Matrix GetMatrix(const PaintState& aState, uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    float sc = ApplyVariation(aState, scale, varIndexBase);
    return Matrix::Scaling(sc, sc);
  }
};

struct PaintScaleUniformAroundCenter : public PaintTransformBase {
  enum { kFormat = 22 };
  F2DOT14 scale;
  FWORD centerX;
  FWORD centerY;

  Matrix GetMatrix(const PaintState& aState, uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    Point center(aState.F2P(int16_t(centerX)), -aState.F2P(int16_t(centerY)));
    return Matrix::Translation(center)
        .PreScale(float(scale), float(scale))
        .PreTranslate(-center);
  }
};

struct PaintVarScaleUniformAroundCenter : public PaintScaleUniformAroundCenter {
  enum { kFormat = 23 };
  uint32 varIndexBase;

  Matrix GetMatrix(const PaintState& aState, uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    float sc = ApplyVariation(aState, scale, varIndexBase);
    Point center(aState.F2P(ApplyVariation(aState, int16_t(centerX),
                                           SatAdd(varIndexBase, 1))),
                 -aState.F2P(ApplyVariation(aState, int16_t(centerY),
                                            SatAdd(varIndexBase, 2))));
    return Matrix::Translation(center).PreScale(sc, sc).PreTranslate(-center);
  }
};

struct PaintRotate : public PaintTransformBase {
  enum { kFormat = 24 };
  F2DOT14 angle;

  Matrix GetMatrix(const PaintState& aState, uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    return Matrix::Rotation(-float(angle) * std::numbers::pi_v<float>);
  }
};

struct PaintVarRotate : public PaintRotate {
  enum { kFormat = 25 };
  uint32 varIndexBase;

  Matrix GetMatrix(const PaintState& aState, uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    float ang = ApplyVariation(aState, angle, varIndexBase);
    return Matrix::Rotation(-ang * std::numbers::pi_v<float>);
  }
};

struct PaintRotateAroundCenter : public PaintTransformBase {
  enum { kFormat = 26 };
  F2DOT14 angle;
  FWORD centerX;
  FWORD centerY;

  Matrix GetMatrix(const PaintState& aState, uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    Point center(aState.F2P(int16_t(centerX)), -aState.F2P(int16_t(centerY)));
    return Matrix::Translation(center)
        .PreRotate(-float(angle) * std::numbers::pi_v<float>)
        .PreTranslate(-center);
  }
};

struct PaintVarRotateAroundCenter : public PaintRotateAroundCenter {
  enum { kFormat = 27 };
  uint32 varIndexBase;

  Matrix GetMatrix(const PaintState& aState, uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    float ang = ApplyVariation(aState, angle, varIndexBase);
    Point center(aState.F2P(ApplyVariation(aState, int16_t(centerX),
                                           SatAdd(varIndexBase, 1))),
                 -aState.F2P(ApplyVariation(aState, int16_t(centerY),
                                            SatAdd(varIndexBase, 2))));
    return Matrix::Translation(center)
        .PreRotate(-ang * std::numbers::pi_v<float>)
        .PreTranslate(-center);
  }
};

static inline Matrix SkewMatrix(float aSkewX, float aSkewY) {
  float xy = tanf(aSkewX * std::numbers::pi_v<float>);
  float yx = tanf(aSkewY * std::numbers::pi_v<float>);
  return std::isnan(xy) || std::isnan(yx) ? Matrix()
                                          : Matrix(1.0, -yx, xy, 1.0, 0.0, 0.0);
}

struct PaintSkew : public PaintTransformBase {
  enum { kFormat = 28 };
  F2DOT14 xSkewAngle;
  F2DOT14 ySkewAngle;

  Matrix GetMatrix(const PaintState& aState, uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    return SkewMatrix(float(xSkewAngle), float(ySkewAngle));
  }
};

struct PaintVarSkew : public PaintSkew {
  enum { kFormat = 29 };
  uint32 varIndexBase;

  Matrix GetMatrix(const PaintState& aState, uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    return SkewMatrix(
        float(ApplyVariation(aState, xSkewAngle, varIndexBase)),
        float(ApplyVariation(aState, ySkewAngle, SatAdd(varIndexBase, 1))));
  }
};

struct PaintSkewAroundCenter : public PaintTransformBase {
  enum { kFormat = 30 };
  F2DOT14 xSkewAngle;
  F2DOT14 ySkewAngle;
  FWORD centerX;
  FWORD centerY;

  Matrix GetMatrix(const PaintState& aState, uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    Point center(aState.F2P(int16_t(centerX)), -aState.F2P(int16_t(centerY)));
    return Matrix::Translation(center)
        .PreMultiply(SkewMatrix(float(xSkewAngle), float(ySkewAngle)))
        .PreTranslate(-center);
  }
};

struct PaintVarSkewAroundCenter : public PaintSkewAroundCenter {
  enum { kFormat = 31 };
  uint32 varIndexBase;

  Matrix GetMatrix(const PaintState& aState, uint32_t aOffset) const {
    MOZ_ASSERT(format == kFormat);
    Point center(aState.F2P(ApplyVariation(aState, int16_t(centerX),
                                           SatAdd(varIndexBase, 2))),
                 -aState.F2P(ApplyVariation(aState, int16_t(centerY),
                                            SatAdd(varIndexBase, 3))));
    return Matrix::Translation(center)
        .PreMultiply(SkewMatrix(
            ApplyVariation(aState, xSkewAngle, varIndexBase),
            ApplyVariation(aState, ySkewAngle, SatAdd(varIndexBase, 1))))
        .PreTranslate(-center);
  }
};

struct PaintComposite {
  enum { kFormat = 32 };
  uint8_t format;
  Offset24 sourcePaintOffset;
  uint8_t compositeMode;
  Offset24 backdropPaintOffset;

  enum {
    COMPOSITE_CLEAR = 0,
    COMPOSITE_SRC = 1,
    COMPOSITE_DEST = 2,
    COMPOSITE_SRC_OVER = 3,
    COMPOSITE_DEST_OVER = 4,
    COMPOSITE_SRC_IN = 5,
    COMPOSITE_DEST_IN = 6,
    COMPOSITE_SRC_OUT = 7,
    COMPOSITE_DEST_OUT = 8,
    COMPOSITE_SRC_ATOP = 9,
    COMPOSITE_DEST_ATOP = 10,
    COMPOSITE_XOR = 11,
    COMPOSITE_PLUS = 12,
    COMPOSITE_SCREEN = 13,
    COMPOSITE_OVERLAY = 14,
    COMPOSITE_DARKEN = 15,
    COMPOSITE_LIGHTEN = 16,
    COMPOSITE_COLOR_DODGE = 17,
    COMPOSITE_COLOR_BURN = 18,
    COMPOSITE_HARD_LIGHT = 19,
    COMPOSITE_SOFT_LIGHT = 20,
    COMPOSITE_DIFFERENCE = 21,
    COMPOSITE_EXCLUSION = 22,
    COMPOSITE_MULTIPLY = 23,
    COMPOSITE_HSL_HUE = 24,
    COMPOSITE_HSL_SATURATION = 25,
    COMPOSITE_HSL_COLOR = 26,
    COMPOSITE_HSL_LUMINOSITY = 27
  };

  bool Paint(const PaintState& aState, uint32_t aDepth, uint32_t aOffset,
             const Rect* aBounds) const {
    MOZ_ASSERT(format == kFormat);
    if (!backdropPaintOffset || !sourcePaintOffset) {
      return true;
    }
    auto mapCompositionMode = [](uint8_t aMode) -> CompositionOp {
      switch (aMode) {
        default:
          return CompositionOp::OP_SOURCE;
        case COMPOSITE_CLEAR:
        case COMPOSITE_SRC:
        case COMPOSITE_DEST:
          MOZ_ASSERT_UNREACHABLE("should have short-circuited");
          return CompositionOp::OP_SOURCE;
        case COMPOSITE_SRC_OVER:
          return CompositionOp::OP_OVER;
        case COMPOSITE_DEST_OVER:
          return CompositionOp::OP_DEST_OVER;
        case COMPOSITE_SRC_IN:
          return CompositionOp::OP_IN;
        case COMPOSITE_DEST_IN:
          return CompositionOp::OP_DEST_IN;
        case COMPOSITE_SRC_OUT:
          return CompositionOp::OP_OUT;
        case COMPOSITE_DEST_OUT:
          return CompositionOp::OP_DEST_OUT;
        case COMPOSITE_SRC_ATOP:
          return CompositionOp::OP_ATOP;
        case COMPOSITE_DEST_ATOP:
          return CompositionOp::OP_DEST_ATOP;
        case COMPOSITE_XOR:
          return CompositionOp::OP_XOR;
        case COMPOSITE_PLUS:
          return CompositionOp::OP_ADD;
        case COMPOSITE_SCREEN:
          return CompositionOp::OP_SCREEN;
        case COMPOSITE_OVERLAY:
          return CompositionOp::OP_OVERLAY;
        case COMPOSITE_DARKEN:
          return CompositionOp::OP_DARKEN;
        case COMPOSITE_LIGHTEN:
          return CompositionOp::OP_LIGHTEN;
        case COMPOSITE_COLOR_DODGE:
          return CompositionOp::OP_COLOR_DODGE;
        case COMPOSITE_COLOR_BURN:
          return CompositionOp::OP_COLOR_BURN;
        case COMPOSITE_HARD_LIGHT:
          return CompositionOp::OP_HARD_LIGHT;
        case COMPOSITE_SOFT_LIGHT:
          return CompositionOp::OP_SOFT_LIGHT;
        case COMPOSITE_DIFFERENCE:
          return CompositionOp::OP_DIFFERENCE;
        case COMPOSITE_EXCLUSION:
          return CompositionOp::OP_EXCLUSION;
        case COMPOSITE_MULTIPLY:
          return CompositionOp::OP_MULTIPLY;
        case COMPOSITE_HSL_HUE:
          return CompositionOp::OP_HUE;
        case COMPOSITE_HSL_SATURATION:
          return CompositionOp::OP_SATURATION;
        case COMPOSITE_HSL_COLOR:
          return CompositionOp::OP_COLOR;
        case COMPOSITE_HSL_LUMINOSITY:
          return CompositionOp::OP_LUMINOSITY;
      }
    };
    // Short-circuit cases:
    if (compositeMode == COMPOSITE_CLEAR) {
      return true;
    }
    if (compositeMode == COMPOSITE_SRC) {
      return DispatchPaint(aState, aDepth, aOffset + sourcePaintOffset,
                           aBounds);
    }
    if (compositeMode == COMPOSITE_DEST) {
      return DispatchPaint(aState, aDepth, aOffset + backdropPaintOffset,
                           aBounds);
    }

    // We need bounds for the temporary surfaces; so if we didn't have
    // explicitly-provided bounds from a clipList entry for the top-level
    // glyph, then we need to determine the bounding rect here.
    Rect bounds = aBounds ? *aBounds : GetBoundingRect(aState, aDepth, aOffset);
    if (bounds.IsEmpty()) {
      return true;
    }
    bounds.RoundOut();

    PaintState state = aState;
    state.mDrawOptions.mCompositionOp = CompositionOp::OP_OVER;
    IntSize intSize(int(bounds.width), int(bounds.height));

    if (!aState.mDrawTarget->CanCreateSimilarDrawTarget(
            intSize, SurfaceFormat::B8G8R8A8)) {
      // We're not going to be able to render this, so just bail out.
      // (Returning true rather than false means we'll just not paint this
      // part of the glyph, but won't return an error and likely fall back
      // to an ugly black blob.)
      return true;
    }

    // Draw the backdrop paint graph to a temporary surface.
    RefPtr backdrop = aState.mDrawTarget->CreateSimilarDrawTarget(
        intSize, SurfaceFormat::B8G8R8A8);
    if (!backdrop) {
      return true;
    }
    backdrop->SetTransform(Matrix::Translation(-bounds.TopLeft()));
    state.mDrawTarget = backdrop;
    if (!DispatchPaint(state, aDepth, aOffset + backdropPaintOffset, &bounds)) {
      return false;
    }

    // Draw the source paint graph to another temp surface.
    RefPtr source = aState.mDrawTarget->CreateSimilarDrawTarget(
        intSize, SurfaceFormat::B8G8R8A8);
    if (!source) {
      return true;
    }
    source->SetTransform(Matrix::Translation(-bounds.TopLeft()));
    state.mDrawTarget = source;
    if (!DispatchPaint(state, aDepth, aOffset + sourcePaintOffset, &bounds)) {
      return false;
    }

    // Composite the source onto the backdrop using the specified operation.
    Rect localBounds(Point(), bounds.Size());
    RefPtr snapshot = source->Snapshot();
    backdrop->SetTransform(Matrix());
    backdrop->DrawSurface(snapshot, localBounds, localBounds,
                          DrawSurfaceOptions(),
                          DrawOptions(1.0, mapCompositionMode(compositeMode)));

    // And copy the composited result to our final destination.
    snapshot = backdrop->Snapshot();
    aState.mDrawTarget->DrawSurface(snapshot, bounds, localBounds);

    return true;
  }

  Rect GetBoundingRect(const PaintState& aState, uint32_t aDepth,
                       uint32_t aOffset) const {
    if (!backdropPaintOffset || !sourcePaintOffset) {
      return Rect();
    }
    // For now, just return the maximal bounds that could result; this could be
    // smarter, returning just one of the rects or their intersection when
    // appropriate for the composite mode in effect.
    return DispatchGetBounds(aState, aDepth, aOffset + backdropPaintOffset)
        .Union(DispatchGetBounds(aState, aDepth, aOffset + sourcePaintOffset));
  }
};

#pragma pack()

const BaseGlyphPaintRecord* COLRv1Header::GetBaseGlyphPaint(
    uint32_t aGlyphId) const {
  const auto* list = baseGlyphList();
  if (!list) {
    return nullptr;
  }
  auto compare = [](const void* key, const void* data) -> int {
    uint32_t glyphId = (uint32_t)(uintptr_t)key;
    const auto* paintRecord =
        reinterpret_cast<const BaseGlyphPaintRecord*>(data);
    uint32_t baseGlyphId = uint16_t(paintRecord->glyphID);
    if (baseGlyphId == glyphId) {
      return 0;
    }
    return baseGlyphId > glyphId ? -1 : 1;
  };
  return reinterpret_cast<const BaseGlyphPaintRecord*>(
      bsearch((void*)(uintptr_t)aGlyphId, list + 1,
              uint32_t(list->numBaseGlyphPaintRecords),
              sizeof(BaseGlyphPaintRecord), compare));
}

#define DO_CASE_VAR(T) \
  DO_CASE(Paint##T);   \
  DO_CASE(PaintVar##T)

// Process paint table at aOffset from start of COLRv1 table.
static bool DispatchPaint(const PaintState& aState, uint32_t aDepth,
                          uint32_t aOffset, const Rect* aBounds) {
  if (aOffset >= aState.mCOLRLength) {
    return false;
  }

  if (aDepth > kPaintRecursionLimit) {
    return false;
  }

  const uint8_t* paint = aState.COLRv1BaseAddr() + aOffset;
  // All paint table formats start with an 8-bit 'format' field.
  uint8_t format = *paint;

#define DO_CASE(T)                                                            \
  case T::kFormat:                                                            \
    return aOffset + sizeof(T) <= aState.mCOLRLength                          \
               ? reinterpret_cast<const T*>(paint)->Paint(aState, aDepth + 1, \
                                                          aOffset, aBounds)   \
               : false

  switch (format) {
    DO_CASE(PaintColrLayers);
    DO_CASE_VAR(Solid);
    DO_CASE_VAR(LinearGradient);
    DO_CASE_VAR(RadialGradient);
    DO_CASE_VAR(SweepGradient);
    DO_CASE(PaintGlyph);
    DO_CASE(PaintColrGlyph);
    DO_CASE_VAR(Transform);
    DO_CASE_VAR(Translate);
    DO_CASE_VAR(Scale);
    DO_CASE_VAR(ScaleAroundCenter);
    DO_CASE_VAR(ScaleUniform);
    DO_CASE_VAR(ScaleUniformAroundCenter);
    DO_CASE_VAR(Rotate);
    DO_CASE_VAR(RotateAroundCenter);
    DO_CASE_VAR(Skew);
    DO_CASE_VAR(SkewAroundCenter);
    DO_CASE(PaintComposite);
    default:
      break;
  }

#undef DO_CASE

  return false;
}

// Get a gfx::Pattern corresponding to the given paint table, if it is a
// simple format that can be used as a fill (not a sub-graph).
static UniquePtr<Pattern> DispatchMakePattern(const PaintState& aState,
                                              uint32_t aOffset) {
  if (aOffset >= aState.mCOLRLength) {
    return nullptr;
  }

  const uint8_t* paint = aState.COLRv1BaseAddr() + aOffset;
  // All paint table formats start with an 8-bit 'format' field.
  uint8_t format = *paint;

#define DO_CASE(T)                                                       \
  case T::kFormat:                                                       \
    return aOffset + sizeof(T) <= aState.mCOLRLength                     \
               ? reinterpret_cast<const T*>(paint)->MakePattern(aState,  \
                                                                aOffset) \
               : nullptr;

  switch (format) {
    DO_CASE_VAR(Solid);
    DO_CASE_VAR(LinearGradient);
    DO_CASE_VAR(RadialGradient);
    DO_CASE_VAR(SweepGradient);
    default:
      break;
  }

#undef DO_CASE

  return nullptr;
}

// Returns the matrix of the transform paint record at aOffset, or the identity
// if there is none. For a transform record, *aWrappedPaint (if given) is set
// to the offset of the paint it applies to, unless it has none.
static Matrix DispatchGetMatrix(const PaintState& aState, uint32_t aOffset,
                                uint32_t* aWrappedPaint) {
  if (aOffset >= aState.mCOLRLength) {
    return Matrix();
  }

  const uint8_t* paint = aState.COLRv1BaseAddr() + aOffset;
  // All paint table formats start with an 8-bit 'format' field.
  uint8_t format = *paint;

#define DO_CASE(T)                                             \
  case T::kFormat: {                                           \
    if (aOffset + sizeof(T) > aState.mCOLRLength) {            \
      return Matrix();                                         \
    }                                                          \
    const auto* transform = reinterpret_cast<const T*>(paint); \
    if (aWrappedPaint && transform->paintOffset) {             \
      *aWrappedPaint = aOffset + transform->paintOffset;       \
    }                                                          \
    return transform->GetMatrix(aState, aOffset);              \
  }

  switch (format) {
    DO_CASE_VAR(Transform);
    DO_CASE_VAR(Translate);
    DO_CASE_VAR(Scale);
    DO_CASE_VAR(ScaleAroundCenter);
    DO_CASE_VAR(ScaleUniform);
    DO_CASE_VAR(ScaleUniformAroundCenter);
    DO_CASE_VAR(Rotate);
    DO_CASE_VAR(RotateAroundCenter);
    DO_CASE_VAR(Skew);
    DO_CASE_VAR(SkewAroundCenter);
    default:
      break;
  }

#undef DO_CASE

  return Matrix();
}

static Rect DispatchGetBounds(const PaintState& aState, uint32_t aDepth,
                              uint32_t aOffset) {
  if (aOffset >= aState.mCOLRLength) {
    return Rect();
  }

  if (aDepth > kPaintRecursionLimit) {
    return Rect();
  }

  // If bounds for this offset were already computed, return them.
  Rect cached;
  if (aState.mBoundsCache->Get(aOffset, &cached)) {
    return cached;
  }

  const uint8_t* paint = aState.COLRv1BaseAddr() + aOffset;
  // All paint table formats start with an 8-bit 'format' field.
  uint8_t format = *paint;

  Rect result;

#define DO_CASE(T)                                                     \
  case T::kFormat:                                                     \
    result = aOffset + sizeof(T) <= aState.mCOLRLength                 \
                 ? reinterpret_cast<const T*>(paint)->GetBoundingRect( \
                       aState, aDepth + 1, aOffset)                    \
                 : Rect();                                             \
    break

  switch (format) {
    DO_CASE(PaintColrLayers);
    DO_CASE_VAR(Solid);
    DO_CASE_VAR(LinearGradient);
    DO_CASE_VAR(RadialGradient);
    DO_CASE_VAR(SweepGradient);
    DO_CASE(PaintGlyph);
    DO_CASE(PaintColrGlyph);
    DO_CASE_VAR(Transform);
    DO_CASE_VAR(Translate);
    DO_CASE_VAR(Scale);
    DO_CASE_VAR(ScaleAroundCenter);
    DO_CASE_VAR(ScaleUniform);
    DO_CASE_VAR(ScaleUniformAroundCenter);
    DO_CASE_VAR(Rotate);
    DO_CASE_VAR(RotateAroundCenter);
    DO_CASE_VAR(Skew);
    DO_CASE_VAR(SkewAroundCenter);
    DO_CASE(PaintComposite);
    default:
      break;
  }

#undef DO_CASE

  aState.mBoundsCache->InsertOrUpdate(aOffset, result);
  return result;
}

#undef DO_CASE_VAR

bool COLRHeader::Validate(uint64_t aLength) const {
  uint64_t count;
  if ((count = numBaseGlyphRecords)) {
    if (baseGlyphRecordsOffset + count * sizeof(BaseGlyphRecord) > aLength) {
      return false;
    }
  }
  if ((count = numLayerRecords)) {
    if (layerRecordsOffset + count * sizeof(LayerRecord) > aLength) {
      return false;
    }
  }
  // Check ordering of baseGlyphRecords, and that layer indices are in bounds.
  int32_t lastGlyphId = -1;
  const auto* baseGlyph = reinterpret_cast<const BaseGlyphRecord*>(
      reinterpret_cast<const char*>(this) + baseGlyphRecordsOffset);
  for (uint16_t i = 0; i < uint16_t(numBaseGlyphRecords); i++, baseGlyph++) {
    uint16_t glyphId = baseGlyph->glyphId;
    if (lastGlyphId >= int32_t(glyphId)) {
      return false;
    }
    if (uint32_t(baseGlyph->firstLayerIndex) + uint32_t(baseGlyph->numLayers) >
        uint32_t(numLayerRecords)) {
      return false;
    }
    lastGlyphId = glyphId;
  }
  // We don't need to validate all the layer paletteEntryIndex fields here,
  // because PaintState.GetColor will range-check them at paint time.
  return true;
}

bool COLRv1Header::Validate(uint64_t aLength) const {
  if (!base.Validate(aLength)) {
    return false;
  }
  if (baseGlyphListOffset + sizeof(BaseGlyphList) > aLength ||
      layerListOffset + sizeof(LayerList) > aLength ||
      clipListOffset + sizeof(ClipList) > aLength ||
      varIndexMapOffset + sizeof(DeltaSetIndexMap) > aLength ||
      itemVariationStoreOffset + sizeof(ItemVariationStore) > aLength) {
    return false;
  }
  const auto* b = baseGlyphList();
  if (b && !b->Validate(this, aLength)) {
    return false;
  }
  const auto* l = layerList();
  if (l && !l->Validate(this, aLength)) {
    return false;
  }
  const auto* c = clipList();
  if (c && !c->Validate(this, aLength)) {
    return false;
  }
  const auto* v = varIndexMap();
  if (v && !v->Validate(this, aLength)) {
    return false;
  }
  const auto* i = itemVariationStore();
  if (i && !i->Validate(this, aLength)) {
    return false;
  }
  return true;
}

bool BaseGlyphList::Validate(const COLRv1Header* aHeader,
                             uint64_t aLength) const {
  uint64_t count = numBaseGlyphPaintRecords;
  if (aHeader->baseGlyphListOffset + sizeof(BaseGlyphList) +
          count * sizeof(BaseGlyphPaintRecord) >
      aLength) {
    return false;
  }
  // Check ordering of baseGlyphPaint records.
  const auto* records = baseGlyphPaintRecords();
  int32_t prevGlyphID = -1;
  for (uint32_t i = 0; i < numBaseGlyphPaintRecords; ++i) {
    const auto& base = records[i];
    if (int32_t(uint16_t(base.glyphID)) <= prevGlyphID) {
      return false;
    }
    prevGlyphID = base.glyphID;
  }
  return true;
}

bool LayerList::Validate(const COLRv1Header* aHeader, uint64_t aLength) const {
  // Check that paintOffsets array fits.
  uint64_t count = numLayers;
  uint32_t listOffset = aHeader->layerListOffset;
  if (listOffset + sizeof(LayerList) + count * sizeof(uint32) > aLength) {
    return false;
  }
  // Check that values in paintOffsets are within bounds.
  const auto* offsets = paintOffsets();
  for (uint32_t i = 0; i < count; i++) {
    if (listOffset + offsets[i] >= aLength) {
      return false;
    }
  }
  return true;
}

bool Clip::Validate(const COLRv1Header* aHeader, uint64_t aLength) const {
  uint32_t offset = aHeader->clipListOffset + clipBoxOffset;
  if (offset >= aLength) {
    return false;
  }
  // ClipBox format begins with a 1-byte format field.
  const uint8_t* box = reinterpret_cast<const uint8_t*>(aHeader) + offset;
  switch (*box) {
    case 1:
      if (offset <= aLength - sizeof(ClipBoxFormat1)) {
        return true;
      }
      break;
    case 2:
      if (offset <= aLength - sizeof(ClipBoxFormat2)) {
        return true;
      }
      break;
    default:
      // unknown ClipBoxFormat
      break;
  }
  return false;
}

bool ClipList::Validate(const COLRv1Header* aHeader, uint64_t aLength) const {
  uint64_t count = numClips;
  if (aHeader->clipListOffset + sizeof(ClipList) + count * sizeof(Clip) >
      aLength) {
    return false;
  }
  // Check ordering of clip records, and that they are within bounds.
  const auto* clipArray = clips();
  int32_t prevEnd = -1;
  for (uint32_t i = 0; i < count; ++i) {
    const auto& clip = clipArray[i];
    if (int32_t(uint16_t(clip.startGlyphID)) <= prevEnd) {
      return false;
    }
    if (!clip.Validate(aHeader, aLength)) {
      return false;
    }
    prevEnd = uint16_t(clip.endGlyphID);
  }
  return true;
}

bool DeltaSetIndexMap::Validate(const COLRv1Header* aHeader,
                                uint64_t aLength) const {
  uint64_t entrySize = ((entryFormat & MAP_ENTRY_SIZE_MASK) >> 4) + 1;
  uint64_t mapCount;
  uint64_t baseSize;
  switch (format) {
    case 0:
      mapCount = uint32_t(v0.mapCount);
      baseSize = 4;
      break;
    case 1:
      mapCount = uint32_t(v1.mapCount);
      baseSize = 6;
      break;
    default:
      return false;
  }
  if (aHeader->varIndexMapOffset + baseSize + mapCount * entrySize > aLength) {
    return false;
  }
  return true;
}

bool ItemVariationStore::Validate(const COLRv1Header* aHeader,
                                  uint64_t aLength) const {
  uint64_t offset = reinterpret_cast<const char*>(this) -
                    reinterpret_cast<const char*>(aHeader);
  if (offset + variationRegionListOffset + sizeof(VariationRegionList) >
      aLength) {
    return false;
  }
  if (!variationRegionList()->Validate(aHeader, aLength)) {
    return false;
  }
  uint16_t count = itemVariationDataCount;
  if (offset + sizeof(ItemVariationStore) + count * sizeof(Offset32) >
      aLength) {
    return false;
  }
  const auto* ivdOffsets = itemVariationDataOffsets();
  for (uint16_t i = 0; i < count; ++i) {
    uint32_t o = ivdOffsets[i];
    if (offset + o + sizeof(ItemVariationData) > aLength) {
      return false;
    }
    const auto* variationData = reinterpret_cast<const ItemVariationData*>(
        reinterpret_cast<const char*>(this) + ivdOffsets[i]);
    if (!variationData->Validate(aHeader, aLength)) {
      return false;
    }
  }
  return true;
}

bool ItemVariationData::Validate(const COLRv1Header* aHeader,
                                 uint64_t aLength) const {
  const char* limit = reinterpret_cast<const char*>(aHeader) + aLength;
  uint16_t riCount = this->regionIndexCount;
  if (reinterpret_cast<const char*>(regionIndexes() + riCount) > limit) {
    return false;
  }
  uint16_t wdCount = this->wordDeltaCount;
  bool longWords = wdCount & LONG_WORDS;
  wdCount &= WORD_DELTA_COUNT_MASK;
  size_t deltaSetSize = (size_t(riCount) + size_t(wdCount)) << longWords;
  uint16_t itemCount = this->itemCount;
  if (itemCount &&
      deltaSetSize > std::numeric_limits<size_t>::max() / itemCount) {
    return false;
  }
  const char* start = reinterpret_cast<const char*>(deltaSets());
  const char* end = start + size_t(itemCount) * deltaSetSize;
  if (end < start || end > limit) {
    return false;
  }
  return true;
}

}  // end anonymous namespace

namespace mozilla::gfx {

bool COLRFonts::ValidateColorGlyphs(hb_blob_t* aCOLR, hb_blob_t* aCPAL) {
  struct ColorRecord {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t alpha;
  };

  struct CPALHeaderVersion0 {
    uint16 version;
    uint16 numPaletteEntries;
    uint16 numPalettes;
    uint16 numColorRecords;
    Offset32 colorRecordsArrayOffset;
    // uint16 	colorRecordIndices[numPalettes];
    const uint16* colorRecordIndices() const {
      return reinterpret_cast<const uint16*>(this + 1);
    }
  };

  unsigned int cpalLength;
  const CPALHeaderVersion0* cpal = reinterpret_cast<const CPALHeaderVersion0*>(
      hb_blob_get_data(aCPAL, &cpalLength));
  if (!cpal || cpalLength < sizeof(CPALHeaderVersion0)) {
    return false;
  }

  // We can handle either version 0 or 1.
  if (uint16_t(cpal->version) > 1) {
    return false;
  }

  uint16_t numPaletteEntries = cpal->numPaletteEntries;
  uint16_t numPalettes = cpal->numPalettes;
  uint16_t numColorRecords = cpal->numColorRecords;
  uint32_t colorRecordsArrayOffset = cpal->colorRecordsArrayOffset;
  const auto* indices = cpal->colorRecordIndices();
  if (colorRecordsArrayOffset >= cpalLength) {
    return false;
  }
  if (!numPaletteEntries || !numPalettes ||
      numColorRecords < numPaletteEntries) {
    return false;
  }
  if (sizeof(ColorRecord) * numColorRecords >
      cpalLength - colorRecordsArrayOffset) {
    return false;
  }
  if (sizeof(uint16) * numPalettes > cpalLength - sizeof(CPALHeaderVersion0)) {
    return false;
  }
  for (uint16_t i = 0; i < numPalettes; ++i) {
    uint32_t index = indices[i];
    if (index + numPaletteEntries > numColorRecords) {
      return false;
    }
  }

  // The additional fields in CPALv1 are not checked here; the harfbuzz code
  // handles reading them safely.

  unsigned int colrLength;
  const COLRHeader* colr =
      reinterpret_cast<const COLRHeader*>(hb_blob_get_data(aCOLR, &colrLength));
  if (!colr || colrLength < sizeof(COLRHeader)) {
    return false;
  }

  if (uint16_t(colr->version) == 1) {
    return colrLength >= sizeof(COLRv1Header) &&
           reinterpret_cast<const COLRv1Header*>(colr)->Validate(colrLength);
  }

  if (uint16_t(colr->version) != 0) {
    // We only support version 1 (above) or version 0 headers.
    return false;
  }

  return colr->Validate(colrLength);
}

const COLRFonts::GlyphLayers* COLRFonts::GetGlyphLayers(hb_blob_t* aCOLR,
                                                        uint32_t aGlyphId) {
  unsigned int length;
  const COLRHeader* colr =
      reinterpret_cast<const COLRHeader*>(hb_blob_get_data(aCOLR, &length));
  // This should never be called unless we have checked that the COLR table is
  // structurally valid, so it will be safe to read the header fields.
  MOZ_RELEASE_ASSERT(colr && length >= sizeof(COLRHeader), "bad COLR table!");
  auto compareBaseGlyph = [](const void* key, const void* data) -> int {
    uint32_t glyphId = (uint32_t)(uintptr_t)key;
    const auto* baseGlyph = reinterpret_cast<const BaseGlyphRecord*>(data);
    uint32_t baseGlyphId = uint16_t(baseGlyph->glyphId);
    if (baseGlyphId == glyphId) {
      return 0;
    }
    return baseGlyphId > glyphId ? -1 : 1;
  };
  return reinterpret_cast<const GlyphLayers*>(
      bsearch((void*)(uintptr_t)aGlyphId, colr->GetBaseGlyphRecords(),
              uint16_t(colr->numBaseGlyphRecords), sizeof(BaseGlyphRecord),
              compareBaseGlyph));
}

bool COLRFonts::PaintGlyphLayers(
    hb_blob_t* aCOLR, hb_face_t* aFace, const GlyphLayers* aLayers,
    DrawTarget* aDrawTarget, layout::TextDrawTarget* aTextDrawer,
    ScaledFont* aScaledFont, DrawOptions aDrawOptions, const Point& aPoint,
    const sRGBColor& aCurrentColor, const nsTArray<hb_color_t>* aColors,
    bool* aUsedCurrentColor) {
  const auto* glyphRecord = reinterpret_cast<const BaseGlyphRecord*>(aLayers);
  // Default to opaque rendering (non-webrender applies alpha with a layer)
  float alpha = 1.0;
  if (aTextDrawer) {
    // defaultColor is the one that comes from CSS, so it has transparency info.
    bool hasComplexTransparency =
        0.0 < aCurrentColor.a && aCurrentColor.a < 1.0;
    if (hasComplexTransparency && uint16_t(glyphRecord->numLayers) > 1) {
      // WebRender doesn't support drawing multi-layer transparent color-glyphs,
      // as it requires compositing all the layers before applying transparency.
      // (pretend to succeed, output doesn't matter, we will emit a blob)
      aTextDrawer->FoundUnsupportedFeature();
      return true;
    }

    // If we get here, then either alpha is 0 or 1, or there's only one layer
    // which shouldn't have composition issues. In all of these cases, applying
    // transparency directly to the glyph should work perfectly fine.
    //
    // Note that we must still emit completely transparent emoji, because they
    // might be wrapped in a shadow that uses the text run's glyphs.
    alpha = aCurrentColor.a;
  }

  unsigned int length;
  const COLRHeader* colr =
      reinterpret_cast<const COLRHeader*>(hb_blob_get_data(aCOLR, &length));
  PaintState state{{colr},
                   aColors->Elements(),
                   aDrawTarget,
                   aScaledFont,
                   nullptr,  // variations not needed
                   aDrawOptions,
                   length,
                   aCurrentColor,
                   0.0,  // fontUnitsToPixels not needed
                   uint16_t(aColors->Length()),
                   0,
                   nullptr,
                   nullptr,
                   aUsedCurrentColor};
  return glyphRecord->Paint(state, alpha, aPoint);
}

const COLRFonts::GlyphPaintGraph* COLRFonts::GetGlyphPaintGraph(
    hb_blob_t* aCOLR, uint32_t aGlyphId) {
  unsigned int blobLength;
  const auto* colr =
      reinterpret_cast<const COLRHeader*>(hb_blob_get_data(aCOLR, &blobLength));
  MOZ_ASSERT(colr, "Cannot get COLR raw data");
  MOZ_ASSERT(blobLength >= sizeof(COLRHeader), "COLR data too small");

  uint16_t version = colr->version;
  if (version == 1) {
    MOZ_ASSERT(blobLength >= sizeof(COLRv1Header), "COLRv1 data too small");
    const auto* colrv1 = reinterpret_cast<const COLRv1Header*>(colr);
    return reinterpret_cast<const GlyphPaintGraph*>(
        colrv1->GetBaseGlyphPaint(aGlyphId));
  }

  return nullptr;
}

bool COLRFonts::PaintGlyphGraph(
    hb_blob_t* aCOLR, hb_font_t* aFont, const GlyphPaintGraph* aPaintGraph,
    DrawTarget* aDrawTarget, layout::TextDrawTarget* aTextDrawer,
    ScaledFont* aScaledFont, DrawOptions aDrawOptions, const Point& aPoint,
    const sRGBColor& aCurrentColor, const nsTArray<hb_color_t>* aColors,
    uint32_t aGlyphId, float aFontUnitsToPixels, bool* aUsedCurrentColor) {
  if (aTextDrawer) {
    // Currently we always punt to a blob for COLRv1 glyphs.
    aTextDrawer->FoundUnsupportedFeature();
    return true;
  }

  unsigned int coordCount;
  const int* coords = hb_font_get_var_coords_normalized(aFont, &coordCount);

  AutoTArray<uint32_t, 32> visitedOffsets;
  nsTHashMap<uint32_t, Rect> boundsCache;
  PaintState state{{nullptr},
                   aColors->Elements(),
                   aDrawTarget,
                   aScaledFont,
                   coords,
                   aDrawOptions,
                   hb_blob_get_length(aCOLR),
                   aCurrentColor,
                   aFontUnitsToPixels,
                   uint16_t(aColors->Length()),
                   uint16_t(coordCount),
                   &visitedOffsets,
                   &boundsCache,
                   aUsedCurrentColor};
  state.mHeader.v1 =
      reinterpret_cast<const COLRv1Header*>(hb_blob_get_data(aCOLR, nullptr));
  AutoRestoreTransform saveTransform(aDrawTarget);
  aDrawTarget->ConcatTransform(Matrix::Translation(aPoint));
  return PaintColrGlyph::DoPaint(
      state, 0, reinterpret_cast<const BaseGlyphPaintRecord*>(aPaintGraph),
      aGlyphId, nullptr);
}

Rect COLRFonts::GetColorGlyphBounds(hb_blob_t* aCOLR, hb_font_t* aFont,
                                    uint32_t aGlyphId, DrawTarget* aDrawTarget,
                                    ScaledFont* aScaledFont,
                                    float aFontUnitsToPixels) {
  unsigned int coordCount;
  const int* coords = hb_font_get_var_coords_normalized(aFont, &coordCount);

  AutoTArray<uint32_t, 32> visitedOffsets;
  nsTHashMap<uint32_t, Rect> boundsCache;
  PaintState state{{nullptr},
                   nullptr,  // palette is not needed
                   aDrawTarget,
                   aScaledFont,
                   coords,
                   DrawOptions(),
                   hb_blob_get_length(aCOLR),
                   sRGBColor(),
                   aFontUnitsToPixels,
                   0,  // numPaletteEntries
                   uint16_t(coordCount),
                   &visitedOffsets,
                   &boundsCache};
  state.mHeader.v1 =
      reinterpret_cast<const COLRv1Header*>(hb_blob_get_data(aCOLR, nullptr));
  MOZ_ASSERT(uint16_t(state.mHeader.v1->base.version) == 1);
  // If a clip rect is provided, return this as the glyph bounds.
  const auto* clipList = state.mHeader.v1->clipList();
  if (clipList) {
    const auto* clip = clipList->GetClip(aGlyphId);
    if (clip) {
      return clip->GetRect(state);
    }
  }
  // Otherwise, compute bounds by walking the paint graph.
  const auto* base = state.mHeader.v1->GetBaseGlyphPaint(aGlyphId);
  if (base) {
    return DispatchGetBounds(
        state, 0, state.mHeader.v1->baseGlyphListOffset + base->paintOffset);
  }
  return Rect();
}

uint16_t COLRFonts::GetColrTableVersion(hb_blob_t* aCOLR) {
  unsigned int blobLength;
  const auto* colr =
      reinterpret_cast<const COLRHeader*>(hb_blob_get_data(aCOLR, &blobLength));
  MOZ_ASSERT(colr, "Cannot get COLR raw data");
  MOZ_ASSERT(blobLength >= sizeof(COLRHeader), "COLR data too small");
  return colr->version;
}

nsTArray<hb_color_t> COLRFonts::CreateColorPalette(
    hb_face_t* aFace, const FontPaletteValueSet* aPaletteValueSet,
    nsAtom* aFontPalette, const nsACString& aFamilyName) {
  // Find the base color palette to use, if there are multiple available;
  // default to first in the font, if nothing matches what is requested.
  unsigned int paletteIndex = 0;
  unsigned int count = hb_ot_color_palette_get_count(aFace);
  MOZ_ASSERT(count > 0, "No palettes? Font should have been rejected!");

  const FontPaletteValueSet::PaletteValues* fpv = nullptr;
  if (aFontPalette && aFontPalette != nsGkAtoms::normal &&
      (count > 1 || aPaletteValueSet)) {
    auto findPalette = [&](hb_ot_color_palette_flags_t flag) -> unsigned int {
      MOZ_ASSERT(flag != HB_OT_COLOR_PALETTE_FLAG_DEFAULT);
      for (unsigned int i = 0; i < count; ++i) {
        if (hb_ot_color_palette_get_flags(aFace, i) & flag) {
          return i;
        }
      }
      return 0;
    };

    if (aFontPalette == nsGkAtoms::light) {
      paletteIndex =
          findPalette(HB_OT_COLOR_PALETTE_FLAG_USABLE_WITH_LIGHT_BACKGROUND);
    } else if (aFontPalette == nsGkAtoms::dark) {
      paletteIndex =
          findPalette(HB_OT_COLOR_PALETTE_FLAG_USABLE_WITH_DARK_BACKGROUND);
    } else {
      if (aPaletteValueSet) {
        if ((fpv = aPaletteValueSet->Lookup(aFontPalette, aFamilyName))) {
          if (fpv->mBasePalette >= 0 && fpv->mBasePalette < int32_t(count)) {
            paletteIndex = fpv->mBasePalette;
          } else if (fpv->mBasePalette ==
                     FontPaletteValueSet::PaletteValues::kLight) {
            paletteIndex = findPalette(
                HB_OT_COLOR_PALETTE_FLAG_USABLE_WITH_LIGHT_BACKGROUND);
          } else if (fpv->mBasePalette ==
                     FontPaletteValueSet::PaletteValues::kDark) {
            paletteIndex = findPalette(
                HB_OT_COLOR_PALETTE_FLAG_USABLE_WITH_DARK_BACKGROUND);
          }
        }
      }
    }
  }

  // Collect the palette colors.
  count =
      hb_ot_color_palette_get_colors(aFace, paletteIndex, 0, nullptr, nullptr);
  nsTArray<hb_color_t> palette;
  // If palette allocation fails, it will remain zero-length, and GetColor()
  // will just return semi-opaque gray for everything. This seems preferable
  // to crashing the process on OOM here.
  if (palette.SetLength(count, fallible)) {
    hb_ot_color_palette_get_colors(aFace, paletteIndex, 0, &count,
                                   palette.Elements());

    // Apply @font-palette-values overrides, if present.
    if (fpv) {
      for (const auto overrideColor : fpv->mOverrides) {
        if (overrideColor.mIndex < palette.Length()) {
          // Override colors contain nscolor, but the palette uses hb_color_t.
          // They have different byte packing orders, so we have to explicitly
          // map the components, not just assign as a 32-bit value.
          nscolor c = overrideColor.mColor;
          palette[overrideColor.mIndex] =
              HB_COLOR(NS_GET_B(c), NS_GET_G(c), NS_GET_R(c), NS_GET_A(c));
        }
      }
    }
  }

  return palette;
}

const FontPaletteValueSet::PaletteValues* FontPaletteValueSet::Lookup(
    nsAtom* aName, const nsACString& aFamily) const {
  nsAutoCString family(aFamily);
  ToLowerCase(family);
  if (const HashEntry* entry =
          mValues.GetEntry(PaletteHashKey(aName, family))) {
    return &entry->mValue;
  }
  return nullptr;
}

FontPaletteValueSet::PaletteValues* FontPaletteValueSet::Insert(
    nsAtom* aName, const nsACString& aFamily) {
  nsAutoCString family(aFamily);
  ToLowerCase(family);
  HashEntry* entry = mValues.PutEntry(PaletteHashKey(aName, family));
  return &entry->mValue;
}

}  // end namespace mozilla::gfx
