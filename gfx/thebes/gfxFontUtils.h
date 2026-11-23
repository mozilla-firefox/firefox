/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_FONT_UTILS_H
#define GFX_FONT_UTILS_H

#include "gfxPlatform.h"
#include "harfbuzz/hb.h"
#include "mozilla/Attributes.h"
#include "mozilla/EndianUtils.h"
#include "nsStringFwd.h"
#include "nsTArray.h"
#include "nscore.h"

class PickleIterator;
class gfxFontEntry;
struct gfxFontVariationAxis;
struct gfxFontVariationInstance;
class gfxSparseBitSet;

namespace mozilla {
class Encoding;
class ServoStyleSet;
class SlantStyleRange;
class WidthRange;
class WeightRange;
struct StyleFontStyle;
struct StyleFontWidth;
struct StyleFontWeight;
}  // namespace mozilla

#undef ERROR /* defined by Windows.h, conflicts with some generated bindings \
                code when this gets indirectly included via shared font list \
              */

typedef struct hb_blob_t hb_blob_t;

#define TRUETYPE_TAG(a, b, c, d) ((a) << 24 | (b) << 16 | (c) << 8 | (d))

namespace mozilla {

// Byte-swapping types and name table structure definitions moved from
// gfxFontUtils.cpp to .h file so that gfxFont.cpp can also refer to them
#pragma pack(1)

struct AutoSwap_PRUint16 {
#ifdef __SUNPRO_CC
  AutoSwap_PRUint16& operator=(const uint16_t aValue) {
    this->value = mozilla::NativeEndian::swapToBigEndian(aValue);
    return *this;
  }
#else
  MOZ_IMPLICIT AutoSwap_PRUint16(uint16_t aValue) {
    value = mozilla::NativeEndian::swapToBigEndian(aValue);
  }
#endif
  operator uint16_t() const {
    return mozilla::NativeEndian::swapFromBigEndian(value);
  }

  operator uint32_t() const {
    return mozilla::NativeEndian::swapFromBigEndian(value);
  }

  operator uint64_t() const {
    return mozilla::NativeEndian::swapFromBigEndian(value);
  }

 private:
  uint16_t value;
};

struct AutoSwap_PRInt16 {
#ifdef __SUNPRO_CC
  AutoSwap_PRInt16& operator=(const int16_t aValue) {
    this->value = mozilla::NativeEndian::swapToBigEndian(aValue);
    return *this;
  }
#else
  MOZ_IMPLICIT AutoSwap_PRInt16(int16_t aValue) {
    value = mozilla::NativeEndian::swapToBigEndian(aValue);
  }
#endif
  operator int16_t() const {
    return mozilla::NativeEndian::swapFromBigEndian(value);
  }

  operator uint32_t() const {
    return mozilla::NativeEndian::swapFromBigEndian(value);
  }

 private:
  int16_t value;
};

struct AutoSwap_PRUint32 {
#ifdef __SUNPRO_CC
  AutoSwap_PRUint32& operator=(const uint32_t aValue) {
    this->value = mozilla::NativeEndian::swapToBigEndian(aValue);
    return *this;
  }
#else
  MOZ_IMPLICIT AutoSwap_PRUint32(uint32_t aValue) {
    value = mozilla::NativeEndian::swapToBigEndian(aValue);
  }
#endif
  operator uint32_t() const {
    return mozilla::NativeEndian::swapFromBigEndian(value);
  }

 private:
  uint32_t value;
};

struct AutoSwap_PRInt32 {
#ifdef __SUNPRO_CC
  AutoSwap_PRInt32& operator=(const int32_t aValue) {
    this->value = mozilla::NativeEndian::swapToBigEndian(aValue);
    return *this;
  }
#else
  MOZ_IMPLICIT AutoSwap_PRInt32(int32_t aValue) {
    value = mozilla::NativeEndian::swapToBigEndian(aValue);
  }
#endif
  operator int32_t() const {
    return mozilla::NativeEndian::swapFromBigEndian(value);
  }

 private:
  int32_t value;
};

struct AutoSwap_PRUint64 {
#ifdef __SUNPRO_CC
  AutoSwap_PRUint64& operator=(const uint64_t aValue) {
    this->value = mozilla::NativeEndian::swapToBigEndian(aValue);
    return *this;
  }
#else
  MOZ_IMPLICIT AutoSwap_PRUint64(uint64_t aValue) {
    value = mozilla::NativeEndian::swapToBigEndian(aValue);
  }
#endif
  operator uint64_t() const {
    return mozilla::NativeEndian::swapFromBigEndian(value);
  }

 private:
  uint64_t value;
};

struct AutoSwap_PRUint24 {
  operator uint32_t() const {
    return value[0] << 16 | value[1] << 8 | value[2];
  }

 private:
  AutoSwap_PRUint24() = default;
  uint8_t value[3];
};

struct SFNTHeader {
  AutoSwap_PRUint32 sfntVersion;    // Fixed, 0x00010000 for version 1.0.
  AutoSwap_PRUint16 numTables;      // Number of tables.
  AutoSwap_PRUint16 searchRange;    // (Maximum power of 2 <= numTables) x 16.
  AutoSwap_PRUint16 entrySelector;  // Log2(maximum power of 2 <= numTables).
  AutoSwap_PRUint16 rangeShift;     // NumTables x 16-searchRange.
};

struct TTCHeader {
  AutoSwap_PRUint32 ttcTag;  // 4 -byte identifier 'ttcf'.
  AutoSwap_PRUint16 majorVersion;
  AutoSwap_PRUint16 minorVersion;
  AutoSwap_PRUint32 numFonts;
  // followed by:
  // AutoSwap_PRUint32 offsetTable[numFonts]
};

struct TableDirEntry {
  AutoSwap_PRUint32 tag;       // 4 -byte identifier.
  AutoSwap_PRUint32 checkSum;  // CheckSum for this table.
  AutoSwap_PRUint32 offset;    // Offset from beginning of TrueType font file.
  AutoSwap_PRUint32 length;    // Length of this table.
};

struct HeadTable {
  enum {
    HEAD_VERSION = 0x00010000,
    HEAD_MAGIC_NUMBER = 0x5F0F3CF5,
    HEAD_CHECKSUM_CALC_CONST = 0xB1B0AFBA
  };

  AutoSwap_PRUint32 tableVersionNumber;  // Fixed, 0x00010000 for version 1.0.
  AutoSwap_PRUint32 fontRevision;        // Set by font manufacturer.
  AutoSwap_PRUint32
      checkSumAdjustment;  // To compute: set it to 0, sum the entire font as
                           // ULONG, then store 0xB1B0AFBA - sum.
  AutoSwap_PRUint32 magicNumber;  // Set to 0x5F0F3CF5.
  AutoSwap_PRUint16 flags;
  AutoSwap_PRUint16
      unitsPerEm;  // Valid range is from 16 to 16384. This value should be a
                   // power of 2 for fonts that have TrueType outlines.
  AutoSwap_PRUint64 created;  // Number of seconds since 12:00 midnight, January
                              // 1, 1904. 64-bit integer
  AutoSwap_PRUint64 modified;       // Number of seconds since 12:00 midnight,
                                    // January 1, 1904. 64-bit integer
  AutoSwap_PRInt16 xMin;            // For all glyph bounding boxes.
  AutoSwap_PRInt16 yMin;            // For all glyph bounding boxes.
  AutoSwap_PRInt16 xMax;            // For all glyph bounding boxes.
  AutoSwap_PRInt16 yMax;            // For all glyph bounding boxes.
  AutoSwap_PRUint16 macStyle;       // Bit 0: Bold (if set to 1);
  AutoSwap_PRUint16 lowestRecPPEM;  // Smallest readable size in pixels.
  AutoSwap_PRInt16 fontDirectionHint;
  AutoSwap_PRInt16 indexToLocFormat;
  AutoSwap_PRInt16 glyphDataFormat;
};

struct OS2Table {
  AutoSwap_PRUint16 version;  // 0004 = OpenType 1.5
  AutoSwap_PRInt16 xAvgCharWidth;
  AutoSwap_PRUint16 usWeightClass;
  AutoSwap_PRUint16 usWidthClass;
  AutoSwap_PRUint16 fsType;
  AutoSwap_PRInt16 ySubscriptXSize;
  AutoSwap_PRInt16 ySubscriptYSize;
  AutoSwap_PRInt16 ySubscriptXOffset;
  AutoSwap_PRInt16 ySubscriptYOffset;
  AutoSwap_PRInt16 ySuperscriptXSize;
  AutoSwap_PRInt16 ySuperscriptYSize;
  AutoSwap_PRInt16 ySuperscriptXOffset;
  AutoSwap_PRInt16 ySuperscriptYOffset;
  AutoSwap_PRInt16 yStrikeoutSize;
  AutoSwap_PRInt16 yStrikeoutPosition;
  AutoSwap_PRInt16 sFamilyClass;
  uint8_t panose[10];
  AutoSwap_PRUint32 unicodeRange1;
  AutoSwap_PRUint32 unicodeRange2;
  AutoSwap_PRUint32 unicodeRange3;
  AutoSwap_PRUint32 unicodeRange4;
  uint8_t achVendID[4];
  AutoSwap_PRUint16 fsSelection;
  AutoSwap_PRUint16 usFirstCharIndex;
  AutoSwap_PRUint16 usLastCharIndex;
  AutoSwap_PRInt16 sTypoAscender;
  AutoSwap_PRInt16 sTypoDescender;
  AutoSwap_PRInt16 sTypoLineGap;
  AutoSwap_PRUint16 usWinAscent;
  AutoSwap_PRUint16 usWinDescent;
  AutoSwap_PRUint32 codePageRange1;
  AutoSwap_PRUint32 codePageRange2;
  AutoSwap_PRInt16 sxHeight;
  AutoSwap_PRInt16 sCapHeight;
  AutoSwap_PRUint16 usDefaultChar;
  AutoSwap_PRUint16 usBreakChar;
  AutoSwap_PRUint16 usMaxContext;
};

struct PostTable {
  AutoSwap_PRUint32 version;
  AutoSwap_PRInt32 italicAngle;
  AutoSwap_PRInt16 underlinePosition;
  AutoSwap_PRUint16 underlineThickness;
  AutoSwap_PRUint32 isFixedPitch;
  AutoSwap_PRUint32 minMemType42;
  AutoSwap_PRUint32 maxMemType42;
  AutoSwap_PRUint32 minMemType1;
  AutoSwap_PRUint32 maxMemType1;
};

// This structure is used for both 'hhea' and 'vhea' tables.
// The field names here are those of the horizontal version; the
// vertical table just exchanges vertical and horizontal coordinates.
struct MetricsHeader {
  AutoSwap_PRUint32 version;
  AutoSwap_PRInt16 ascender;
  AutoSwap_PRInt16 descender;
  AutoSwap_PRInt16 lineGap;
  AutoSwap_PRUint16 advanceWidthMax;
  AutoSwap_PRInt16 minLeftSideBearing;
  AutoSwap_PRInt16 minRightSideBearing;
  AutoSwap_PRInt16 xMaxExtent;
  AutoSwap_PRInt16 caretSlopeRise;
  AutoSwap_PRInt16 caretSlopeRun;
  AutoSwap_PRInt16 caretOffset;
  AutoSwap_PRInt16 reserved1;
  AutoSwap_PRInt16 reserved2;
  AutoSwap_PRInt16 reserved3;
  AutoSwap_PRInt16 reserved4;
  AutoSwap_PRInt16 metricDataFormat;
  AutoSwap_PRUint16 numOfLongMetrics;
};

struct MaxpTableHeader {
  AutoSwap_PRUint32 version;  // CFF: 0x00005000; TrueType: 0x00010000
  AutoSwap_PRUint16 numGlyphs;
  // truetype version has additional fields that we don't currently use
};

// old 'kern' table, supported on Windows
// see http://www.microsoft.com/typography/otspec/kern.htm
struct KernTableVersion0 {
  AutoSwap_PRUint16 version;  // 0x0000
  AutoSwap_PRUint16 nTables;
};

struct KernTableSubtableHeaderVersion0 {
  AutoSwap_PRUint16 version;
  AutoSwap_PRUint16 length;
  AutoSwap_PRUint16 coverage;
};

// newer Mac-only 'kern' table, ignored by Windows
// see http://developer.apple.com/textfonts/TTRefMan/RM06/Chap6kern.html
struct KernTableVersion1 {
  AutoSwap_PRUint32 version;  // 0x00010000
  AutoSwap_PRUint32 nTables;
};

struct KernTableSubtableHeaderVersion1 {
  AutoSwap_PRUint32 length;
  AutoSwap_PRUint16 coverage;
  AutoSwap_PRUint16 tupleIndex;
};

#pragma pack()

// Return just the highest bit of the given value, i.e., the highest
// power of 2 that is <= value, or zero if the input value is zero.
inline uint32_t FindHighestBit(uint32_t value) {
  // propagate highest bit into all lower bits of the value
  value |= (value >> 1);
  value |= (value >> 2);
  value |= (value >> 4);
  value |= (value >> 8);
  value |= (value >> 16);
  // isolate the leftmost bit
  return (value & ~(value >> 1));
}

}  // namespace mozilla

// used for overlaying name changes without touching original font data
struct FontDataOverlay {
  // overlaySrc != 0 ==> use overlay
  uint32_t overlaySrc;     // src offset from start of font data
  uint32_t overlaySrcLen;  // src length
  uint32_t overlayDest;    // dest offset from start of font data
};

enum gfxUserFontType {
  GFX_USERFONT_UNKNOWN = 0,
  GFX_USERFONT_OPENTYPE = 1,
  GFX_USERFONT_SVG = 2,
  GFX_USERFONT_WOFF = 3,
  GFX_USERFONT_WOFF2 = 4
};

extern const uint8_t sCJKCompatSVSTable[];

class gfxFontUtils {
 public:
  // these are public because gfxFont.cpp also looks into the name table
  enum {
    NAME_ID_FAMILY = 1,
    NAME_ID_STYLE = 2,
    NAME_ID_UNIQUE = 3,
    NAME_ID_FULL = 4,
    NAME_ID_VERSION = 5,
    NAME_ID_POSTSCRIPT = 6,
    NAME_ID_PREFERRED_FAMILY = 16,
    NAME_ID_PREFERRED_STYLE = 17,

    PLATFORM_ALL = -1,
    PLATFORM_ID_UNICODE = 0,  // Mac OS uses this typically
    PLATFORM_ID_MAC = 1,
    PLATFORM_ID_ISO = 2,
    PLATFORM_ID_MICROSOFT = 3,

    ENCODING_ID_MAC_ROMAN = 0,  // traditional Mac OS script manager encodings
    ENCODING_ID_MAC_JAPANESE =
        1,  // (there are others defined, but some were never
    ENCODING_ID_MAC_TRAD_CHINESE =
        2,  // implemented by Apple, and I have never seen them
    ENCODING_ID_MAC_KOREAN = 3,  // used in font names)
    ENCODING_ID_MAC_ARABIC = 4,
    ENCODING_ID_MAC_HEBREW = 5,
    ENCODING_ID_MAC_GREEK = 6,
    ENCODING_ID_MAC_CYRILLIC = 7,
    ENCODING_ID_MAC_DEVANAGARI = 9,
    ENCODING_ID_MAC_GURMUKHI = 10,
    ENCODING_ID_MAC_GUJARATI = 11,
    ENCODING_ID_MAC_SIMP_CHINESE = 25,

    ENCODING_ID_MICROSOFT_SYMBOL = 0,  // Microsoft platform encoding IDs
    ENCODING_ID_MICROSOFT_UNICODEBMP = 1,
    ENCODING_ID_MICROSOFT_SHIFTJIS = 2,
    ENCODING_ID_MICROSOFT_PRC = 3,
    ENCODING_ID_MICROSOFT_BIG5 = 4,
    ENCODING_ID_MICROSOFT_WANSUNG = 5,
    ENCODING_ID_MICROSOFT_JOHAB = 6,
    ENCODING_ID_MICROSOFT_UNICODEFULL = 10,

    LANG_ALL = -1,
    LANG_ID_MAC_ENGLISH = 0,  // many others are defined, but most don't affect
    LANG_ID_MAC_HEBREW =
        10,  // the charset; should check all the central/eastern
    LANG_ID_MAC_JAPANESE = 11,  // european codes, though
    LANG_ID_MAC_ARABIC = 12,
    LANG_ID_MAC_ICELANDIC = 15,
    LANG_ID_MAC_TURKISH = 17,
    LANG_ID_MAC_TRAD_CHINESE = 19,
    LANG_ID_MAC_URDU = 20,
    LANG_ID_MAC_KOREAN = 23,
    LANG_ID_MAC_POLISH = 25,
    LANG_ID_MAC_FARSI = 31,
    LANG_ID_MAC_SIMP_CHINESE = 33,
    LANG_ID_MAC_ROMANIAN = 37,
    LANG_ID_MAC_CZECH = 38,
    LANG_ID_MAC_SLOVAK = 39,

    LANG_ID_MICROSOFT_EN_US =
        0x0409,  // with Microsoft platformID, EN US lang code

    CMAP_MAX_CODEPOINT = 0x10ffff  // maximum possible Unicode codepoint
                                   // contained in a cmap
  };

  // name table has a header, followed by name records, followed by string data
  struct NameHeader {
    mozilla::AutoSwap_PRUint16 format;        // Format selector (=0).
    mozilla::AutoSwap_PRUint16 count;         // Number of name records.
    mozilla::AutoSwap_PRUint16 stringOffset;  // Offset to start of string
                                              // storage (from start of table)
  };

  struct NameRecord {
    mozilla::AutoSwap_PRUint16 platformID;  // Platform ID
    mozilla::AutoSwap_PRUint16 encodingID;  // Platform-specific encoding ID
    mozilla::AutoSwap_PRUint16 languageID;  // Language ID
    mozilla::AutoSwap_PRUint16 nameID;      // Name ID.
    mozilla::AutoSwap_PRUint16 length;      // String length (in bytes).
    mozilla::AutoSwap_PRUint16 offset;  // String offset from start of storage
                                        // (in bytes).
  };

  // Helper to ensure we free a font table when we return.
  class AutoHBBlob {
   public:
    explicit AutoHBBlob(hb_blob_t* aBlob) : mBlob(aBlob) {}

    ~AutoHBBlob() { hb_blob_destroy(mBlob); }

    operator hb_blob_t*() { return mBlob; }

   private:
    hb_blob_t* const mBlob;
  };

  // for reading big-endian font data on either big or little-endian platforms

  static inline uint16_t ReadShortAt(const uint8_t* aBuf, uint32_t aIndex) {
    return static_cast<uint16_t>(aBuf[aIndex] << 8) | aBuf[aIndex + 1];
  }

  static inline uint16_t ReadShortAt16(const uint16_t* aBuf, uint32_t aIndex) {
    const uint8_t* buf = reinterpret_cast<const uint8_t*>(aBuf);
    uint32_t index = aIndex << 1;
    return static_cast<uint16_t>(buf[index] << 8) | buf[index + 1];
  }

  static inline uint32_t ReadUint24At(const uint8_t* aBuf, uint32_t aIndex) {
    return ((aBuf[aIndex] << 16) | (aBuf[aIndex + 1] << 8) |
            (aBuf[aIndex + 2]));
  }

  static inline uint32_t ReadLongAt(const uint8_t* aBuf, uint32_t aIndex) {
    return ((aBuf[aIndex] << 24) | (aBuf[aIndex + 1] << 16) |
            (aBuf[aIndex + 2] << 8) | (aBuf[aIndex + 3]));
  }

  static nsresult ReadCMAPTableFormat10(const uint8_t* aBuf, uint32_t aLength,
                                        gfxSparseBitSet& aCharacterMap);

  static nsresult ReadCMAPTableFormat12or13(const uint8_t* aBuf,
                                            uint32_t aLength,
                                            gfxSparseBitSet& aCharacterMap);

  static nsresult ReadCMAPTableFormat4(const uint8_t* aBuf, uint32_t aLength,
                                       gfxSparseBitSet& aCharacterMap,
                                       bool aIsSymbolFont);

  static nsresult ReadCMAPTableFormat14(const uint8_t* aBuf, uint32_t aLength,
                                        const uint8_t*& aTable);

  static uint32_t FindPreferredSubtable(const uint8_t* aBuf,
                                        uint32_t aBufLength,
                                        uint32_t* aTableOffset,
                                        uint32_t* aUVSTableOffset,
                                        bool* aIsSymbolFont);

  static nsresult ReadCMAP(const uint8_t* aBuf, uint32_t aBufLength,
                           gfxSparseBitSet& aCharacterMap,
                           uint32_t& aUVSOffset);

  static uint32_t MapCharToGlyphFormat4(const uint8_t* aBuf, uint32_t aLength,
                                        char16_t aCh);

  static uint32_t MapCharToGlyphFormat10(const uint8_t* aBuf, uint32_t aCh);

  static uint32_t MapCharToGlyphFormat12or13(const uint8_t* aBuf, uint32_t aCh);

  static uint16_t MapUVSToGlyphFormat14(const uint8_t* aBuf, uint32_t aCh,
                                        uint32_t aVS);

  // Return whether <aCh, aVS> is supported as the default variation for aCh.
  static bool IsDefaultUVSSequence(const uint8_t* aBuf, uint32_t aCh,
                                   uint32_t aVS);

  // sCJKCompatSVSTable is a 'cmap' format 14 subtable that maps
  // <char + var-selector> pairs to the corresponding Unicode
  // compatibility ideograph codepoints.
  static MOZ_ALWAYS_INLINE uint32_t GetUVSFallback(uint32_t aCh, uint32_t aVS) {
    aCh = MapUVSToGlyphFormat14(sCJKCompatSVSTable, aCh, aVS);
    return aCh >= 0xFB00 ? aCh + (0x2F800 - 0xFB00) : aCh;
  }

  static uint32_t MapCharToGlyph(const uint8_t* aCmapBuf, uint32_t aBufLength,
                                 uint32_t aUnicode, uint32_t aVarSelector = 0);

  // For legacy MS Symbol fonts, we try mapping 8-bit character codes to the
  // Private Use range at U+F0xx used by the cmaps in these fonts.
  static MOZ_ALWAYS_INLINE uint32_t MapLegacySymbolFontCharToPUA(uint32_t aCh) {
    return aCh >= 0x20 && aCh <= 0xff ? 0xf000 + aCh : 0;
  }

#ifdef XP_WIN
  // determine whether a font (which has already been sanitized, so is known
  // to be a valid sfnt) is CFF format rather than TrueType
  static bool IsCffFont(const uint8_t* aFontData);
#endif

  // determine the format of font data
  static gfxUserFontType DetermineFontDataType(const uint8_t* aFontData,
                                               uint32_t aFontDataLength);

  // Read the fullname from the sfnt data (used to save the original name
  // prior to renaming the font for installation).
  // This is called with sfnt data that has already been validated,
  // so it should always succeed in finding the name table.
  static nsresult GetFullNameFromSFNT(const uint8_t* aFontData,
                                      uint32_t aLength, nsACString& aFullName);

  // helper to get fullname from name table, constructing from family+style
  // if no explicit fullname is present
  static nsresult GetFullNameFromTable(hb_blob_t* aNameTable,
                                       nsACString& aFullName);

  // helper to get family name from name table
  static nsresult GetFamilyNameFromTable(hb_blob_t* aNameTable,
                                         nsACString& aFamilyName);

  // Find the table directory entry for a given table tag, in a (validated)
  // buffer of 'sfnt' data. Returns null if the tag is not present.
  static mozilla::TableDirEntry* FindTableDirEntry(const void* aFontData,
                                                   uint32_t aTableTag);

  // Return a blob that wraps a table found within a buffer of font data.
  // The blob does NOT own its data; caller guarantees that the buffer
  // will remain valid at least as long as the blob.
  // Returns null if the specified table is not found.
  // This method assumes aFontData is valid 'sfnt' data; before using this,
  // caller is responsible to do any sanitization/validation necessary.
  static hb_blob_t* GetTableFromFontData(const void* aFontData,
                                         uint32_t aTableTag);

  // create a new name table and build a new font with that name table
  // appended on the end, returns true on success
  static nsresult RenameFont(const nsAString& aName, const uint8_t* aFontData,
                             uint32_t aFontDataLength,
                             FallibleTArray<uint8_t>* aNewFont);

  // read all names matching aNameID, returning in aNames array
  static nsresult ReadNames(const char* aNameData, uint32_t aDataLen,
                            uint32_t aNameID, int32_t aPlatformID,
                            nsTArray<nsCString>& aNames);

  // reads English or first name matching aNameID, returning in aName
  // platform based on OS
  static nsresult ReadCanonicalName(hb_blob_t* aNameTable, uint32_t aNameID,
                                    nsCString& aName);

  static nsresult ReadCanonicalName(const char* aNameData, uint32_t aDataLen,
                                    uint32_t aNameID, nsCString& aName);

  // convert a name from the raw name table data into an nsString,
  // provided we know how; return true if successful, or false
  // if we can't handle the encoding
  static bool DecodeFontName(const char* aBuf, int32_t aLength,
                             uint32_t aPlatformCode, uint32_t aScriptCode,
                             uint32_t aLangCode, nsACString& dest);

  static inline bool IsJoinCauser(uint32_t ch) { return (ch == 0x200D); }

  // We treat Combining Grapheme Joiner (U+034F) together with the join
  // controls (ZWJ, ZWNJ) here, because (like them) it is an invisible
  // char that will be handled by the shaper even if not explicitly
  // supported by the font. (See bug 1408366.)
  static inline bool IsJoinControl(uint32_t ch) {
    return (ch == 0x200C || ch == 0x200D || ch == 0x034f);
  }

  enum {
    kUnicodeVS1 = 0xFE00,
    kUnicodeVS16 = 0xFE0F,
    kUnicodeVS17 = 0xE0100,
    kUnicodeVS256 = 0xE01EF
  };

  static inline bool IsVarSelector(uint32_t ch) {
    return (ch >= kUnicodeVS1 && ch <= kUnicodeVS16) ||
           (ch >= kUnicodeVS17 && ch <= kUnicodeVS256);
  }

  enum {
    kUnicodeRegionalIndicatorA = 0x1F1E6,
    kUnicodeRegionalIndicatorZ = 0x1F1FF
  };

  static inline bool IsRegionalIndicator(uint32_t aCh) {
    return aCh >= kUnicodeRegionalIndicatorA &&
           aCh <= kUnicodeRegionalIndicatorZ;
  }

  static inline bool IsEmojiFlagAndTag(uint32_t aCh, uint32_t aNext) {
    constexpr uint32_t kBlackFlag = 0x1F3F4;
    constexpr uint32_t kTagLetterA = 0xE0061;
    constexpr uint32_t kTagLetterZ = 0xE007A;

    return aCh == kBlackFlag && aNext >= kTagLetterA && aNext <= kTagLetterZ;
  }

  // parse a simple list of font family names into
  // an array of strings
  static void ParseFontList(const nsACString& aFamilyList,
                            nsTArray<nsCString>& aFontList);

  // for a given pref name, initialize a list of font names
  static void GetPrefsFontList(const char* aPrefName,
                               nsTArray<nsCString>& aFontList);

  // generate a unique font name
  static nsresult MakeUniqueUserFontName(nsACString& aName);

  // Helper used to implement gfxFontEntry::GetVariation{Axes,Instances} for
  // platforms where the native font APIs don't provide the info we want
  // in a convenient form, or when native APIs are too expensive.
  // (Not used on platforms -- currently, freetype -- where the font APIs
  // expose variation instance details directly.)
  static void GetVariationData(gfxFontEntry* aFontEntry,
                               nsTArray<gfxFontVariationAxis>* aAxes,
                               nsTArray<gfxFontVariationInstance>* aInstances);

  // Helper method for reading localized family names from the name table
  // of a single face.
  static void ReadOtherFamilyNamesForFace(
      const nsACString& aFamilyName, const char* aNameData,
      uint32_t aDataLength, nsTArray<nsCString>& aOtherFamilyNames,
      bool useFullName);

  // Main, DOM worker or servo thread safe method to check if we are performing
  // Servo traversal.
  static bool IsInServoTraversal();

  // Main, DOM worker or servo thread safe method to get the current
  // ServoTypeSet. Always returns nullptr for DOM worker threads.
  static mozilla::ServoStyleSet* CurrentServoStyleSet();

  static void AssertSafeThreadOrServoFFILocked()
#ifdef DEBUG
      ;
#else
  {
  }
#endif

 protected:
  friend struct MacCharsetMappingComparator;

  static nsresult ReadNames(const char* aNameData, uint32_t aDataLen,
                            uint32_t aNameID, int32_t aLangID,
                            int32_t aPlatformID, nsTArray<nsCString>& aNames);

  // convert opentype name-table platform/encoding/language values to an
  // Encoding object we can use to convert the name data to unicode
  static const mozilla::Encoding* GetCharsetForFontName(uint16_t aPlatform,
                                                        uint16_t aScript,
                                                        uint16_t aLanguage);

  struct MacFontNameCharsetMapping {
    uint16_t mScript;
    uint16_t mLanguage;
    const mozilla::Encoding* mEncoding;

    bool operator<(const MacFontNameCharsetMapping& rhs) const {
      return (mScript < rhs.mScript) ||
             ((mScript == rhs.mScript) && (mLanguage < rhs.mLanguage));
    }
  };
  static const MacFontNameCharsetMapping gMacFontNameCharsets[];
  static const mozilla::Encoding* gISOFontNameCharsets[];
  static const mozilla::Encoding* gMSFontNameCharsets[];
};

// Factors used to weight the distances between the available and target font
// properties during font-matching. These ensure that we respect the CSS-fonts
// requirement that font-width >> font-style >> font-weight; and in addition,
// a mismatch between the desired and actual glyph presentation (emoji vs text)
// will take precedence over any of the style attributes.
constexpr double kPresentationMismatch = 1.0e12;
constexpr double kWidthFactor = 1.0e8;
constexpr double kStyleFactor = 1.0e4;
constexpr double kWeightFactor = 1.0e0;

// If the output range of this function is extended, check assertions &
// usage at the callsites!
// style distance ==> [0,900]
double StyleDistance(const mozilla::SlantStyleRange& aRange,
                     const mozilla::StyleFontStyle& aTargetStyle,
                     bool aItalicToObliqueFallback);

// width distance ==> [0,2000]
double WidthDistance(const mozilla::WidthRange& aRange,
                     const mozilla::StyleFontWidth& aTargetWidth);

// weight distance ==> [0,1600]
double WeightDistance(const mozilla::WeightRange& aRange,
                      const mozilla::StyleFontWeight& aTargetWeight);

#endif /* GFX_FONT_UTILS_H */
