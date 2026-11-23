/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ScaledFontMac.h"

#include "UnscaledFontMac.h"
#include "mozilla/webrender/WebRenderTypes.h"
#ifdef MOZ_WIDGET_COCOA
#  include "nsCocoaFeatures.h"
#endif
#include <dlfcn.h>

#include <vector>

#include "PathSkia.h"
#include "skia/include/core/SkFont.h"
#include "skia/include/core/SkFontTypes.h"
#include "skia/include/core/SkPaint.h"
#include "skia/include/core/SkPath.h"
#include "skia/include/ports/SkTypeface_mac.h"
#ifdef MOZ_WIDGET_UIKIT
#  include <CoreFoundation/CoreFoundation.h>
#endif
#include "mozilla/gfx/Logging.h"

#ifdef MOZ_WIDGET_COCOA
// prototype for private API
extern "C" {
CGPathRef CGFontGetGlyphPath(CGFontRef fontRef,
                             CGAffineTransform* textTransform, int unknown,
                             CGGlyph glyph);
};
#endif

#include "cairo-quartz.h"

namespace mozilla {
namespace gfx {

// Simple helper class to automatically release a CFObject when it goes out
// of scope.
template <class T>
class AutoRelease final {
 public:
  explicit AutoRelease(T aObject) : mObject(aObject) {}

  ~AutoRelease() {
    if (mObject) {
      CFRelease(mObject);
    }
  }

  AutoRelease<T>& operator=(const T& aObject) {
    if (aObject != mObject) {
      if (mObject) {
        CFRelease(mObject);
      }
      mObject = aObject;
    }
    return *this;
  }

  operator T() { return mObject; }

  T forget() {
    T obj = mObject;
    mObject = nullptr;
    return obj;
  }

 private:
  T mObject;
};

// Helper to create a CTFont from a CGFont, copying any variations that were
// set on the CGFont, and applying attributes from (optional) aFontDesc.
CTFontRef CreateCTFontFromCGFontWithVariations(CGFontRef aCGFont, CGFloat aSize,
                                               bool aInstalledFont,
                                               CTFontDescriptorRef aFontDesc) {
#ifdef MOZ_WIDGET_COCOA
  // New implementation (see bug 1856035) for macOS 13+.
  if (nsCocoaFeatures::OnVenturaOrLater()) {
    // Create CTFont, applying any descriptor that was passed (used by
    // gfxCoreTextShaper to set features).
    AutoRelease<CTFontRef> ctFont(
        CTFontCreateWithGraphicsFont(aCGFont, aSize, nullptr, aFontDesc));
    AutoRelease<CFDictionaryRef> vars(CGFontCopyVariations(aCGFont));
    if (vars) {
      // Create an attribute dictionary containing the variations.
      AutoRelease<CFDictionaryRef> attrs(CFDictionaryCreate(
          nullptr, (const void**)&kCTFontVariationAttribute,
          (const void**)&vars, 1, &kCFTypeDictionaryKeyCallBacks,
          &kCFTypeDictionaryValueCallBacks));
      // Get the original descriptor from the CTFont, then add the variations
      // attribute to it.
      AutoRelease<CTFontDescriptorRef> desc(CTFontCopyFontDescriptor(ctFont));
      desc = CTFontDescriptorCreateCopyWithAttributes(desc, attrs);
      // Return a copy of the font that has the variations added.
      return CTFontCreateCopyWithAttributes(ctFont, 0.0, nullptr, desc);
    }
    // No variations to set, just return the default CTFont.
    return ctFont.forget();
  }
#endif

  // Older implementation used up to macOS 12.
  CTFontRef ctFont;
  if (aInstalledFont) {
    AutoRelease<CFDictionaryRef> vars(CGFontCopyVariations(aCGFont));
    if (vars) {
      AutoRelease<CFDictionaryRef> varAttr(CFDictionaryCreate(
          nullptr, (const void**)&kCTFontVariationAttribute,
          (const void**)&vars, 1, &kCFTypeDictionaryKeyCallBacks,
          &kCFTypeDictionaryValueCallBacks));

      AutoRelease<CTFontDescriptorRef> varDesc(
          aFontDesc
              ? ::CTFontDescriptorCreateCopyWithAttributes(aFontDesc, varAttr)
              : ::CTFontDescriptorCreateWithAttributes(varAttr));

      ctFont = CTFontCreateWithGraphicsFont(aCGFont, aSize, nullptr, varDesc);
    } else {
      ctFont = CTFontCreateWithGraphicsFont(aCGFont, aSize, nullptr, nullptr);
    }
  } else {
    ctFont = CTFontCreateWithGraphicsFont(aCGFont, aSize, nullptr, nullptr);
  }
  return ctFont;
}

ScaledFontMac::ScaledFontMac(CGFontRef aFont,
                             const RefPtr<UnscaledFont>& aUnscaledFont,
                             Float aSize, bool aOwnsFont,
                             bool aUseFontSmoothing, bool aApplySyntheticBold,
                             bool aHasColorGlyphs)
    : ScaledFontBase(aUnscaledFont, aSize),
      mFont(aFont),
      mUseFontSmoothing(aUseFontSmoothing),
      mApplySyntheticBold(aApplySyntheticBold),
      mHasColorGlyphs(aHasColorGlyphs) {
  if (!aOwnsFont) {
    // XXX: should we be taking a reference
    CGFontRetain(aFont);
  }

  auto unscaledMac = static_cast<UnscaledFontMac*>(aUnscaledFont.get());
  bool dataFont = unscaledMac->IsDataFont();
  mCTFont = CreateCTFontFromCGFontWithVariations(aFont, aSize, !dataFont);
}

ScaledFontMac::ScaledFontMac(CTFontRef aFont,
                             const RefPtr<UnscaledFont>& aUnscaledFont,
                             bool aUseFontSmoothing, bool aApplySyntheticBold,
                             bool aHasColorGlyphs)
    : ScaledFontBase(aUnscaledFont, CTFontGetSize(aFont)),
      mCTFont(aFont),
      mUseFontSmoothing(aUseFontSmoothing),
      mApplySyntheticBold(aApplySyntheticBold),
      mHasColorGlyphs(aHasColorGlyphs) {
  mFont = CTFontCopyGraphicsFont(aFont, nullptr);

  CFRetain(mCTFont);
}

ScaledFontMac::~ScaledFontMac() {
  CFRelease(mCTFont);
  CGFontRelease(mFont);
}

SkTypeface* ScaledFontMac::CreateSkTypeface() {
  return SkMakeTypefaceFromCTFont(mCTFont).release();
}

void ScaledFontMac::SetupSkFontDrawOptions(SkFont& aFont) {
  aFont.setSubpixel(true);

  // Normally, Skia enables LCD FontSmoothing which creates thicker fonts
  // and also enables subpixel AA. CoreGraphics without font smoothing
  // explicitly creates thinner fonts and grayscale AA.
  // CoreGraphics doesn't support a configuration that produces thicker
  // fonts with grayscale AA as LCD Font Smoothing enables or disables
  // both. However, Skia supports it by enabling font smoothing (producing
  // subpixel AA) and converts it to grayscale AA. Since Skia doesn't
  // support subpixel AA on transparent backgrounds, we still want font
  // smoothing for the thicker fonts, even if it is grayscale AA.
  //
  // With explicit Grayscale AA (from -moz-osx-font-smoothing:grayscale),
  // we want to have grayscale AA with no smoothing at all. This means
  // disabling the LCD font smoothing behaviour.
  // To accomplish this we have to explicitly disable hinting,
  // and disable LCDRenderText.
  if (aFont.getEdging() == SkFont::Edging::kAntiAlias && !mUseFontSmoothing) {
    aFont.setHinting(SkFontHinting::kNone);
  }
}

// private API here are the public options on OS X
// CTFontCreatePathForGlyph
// ATSUGlyphGetCubicPaths
// we've used this in cairo sucessfully for some time.
// Note: cairo dlsyms it. We could do that but maybe it's
// safe just to use?

already_AddRefed<Path> ScaledFontMac::GetPathForGlyphs(
    const GlyphBuffer& aBuffer, const DrawTarget* aTarget) {
  return ScaledFontBase::GetPathForGlyphs(aBuffer, aTarget);
}

static uint32_t CalcTableChecksum(const uint32_t* tableStart, uint32_t length,
                                  bool skipChecksumAdjust = false) {
  uint32_t sum = 0L;
  const uint32_t* table = tableStart;
  const uint32_t* end = table + length / sizeof(uint32_t);
  while (table < end) {
    if (skipChecksumAdjust && (table - tableStart) == 2) {
      table++;
    } else {
      sum += CFSwapInt32BigToHost(*table++);
    }
  }

  // The length is not 4-byte aligned, but we still must process the remaining
  // bytes.
  if (length & 3) {
    // Pad with zero before adding to the checksum.
    uint32_t last = 0;
    memcpy(&last, end, length & 3);
    sum += CFSwapInt32BigToHost(last);
  }

  return sum;
}

struct TableRecord {
  uint32_t tag;
  uint32_t checkSum;
  uint32_t offset;
  uint32_t length;
  CFDataRef data;
};

static int maxPow2LessThanEqual(int a) {
  int x = 1;
  int shift = 0;
  while ((x << (shift + 1)) <= a) {
    shift++;
  }
  return shift;
}

struct writeBuf final {
  explicit writeBuf(int size) {
    this->data = new unsigned char[size];
    this->offset = 0;
  }
  ~writeBuf() { delete[] this->data; }

  template <class T>
  void writeElement(T a) {
    *reinterpret_cast<T*>(&this->data[this->offset]) = a;
    this->offset += sizeof(T);
  }

  void writeMem(const void* data, unsigned long length) {
    memcpy(&this->data[this->offset], data, length);
    this->offset += length;
  }

  void align() {
    while (this->offset & 3) {
      this->data[this->offset] = 0;
      this->offset++;
    }
  }

  unsigned char* data;
  int offset;
};

bool UnscaledFontMac::GetFontFileData(FontFileDataOutput aDataCallback,
                                      void* aBaton) {
  // We'll reconstruct a TTF font from the tables we can get from the CGFont
  CFArrayRef tags = CGFontCopyTableTags(mFont);
  CFIndex count = CFArrayGetCount(tags);

  TableRecord* records = new TableRecord[count];
  uint32_t offset = 0;
  offset += sizeof(uint32_t) * 3;
  offset += sizeof(uint32_t) * 4 * count;
  bool CFF = false;
  for (CFIndex i = 0; i < count; i++) {
    uint32_t tag = (uint32_t)(uintptr_t)CFArrayGetValueAtIndex(tags, i);
    if (tag == 0x43464620 || tag == 0x43464632) {  // 'CFF ', 'CFF2'
      CFF = true;
    }
    CFDataRef data = CGFontCopyTableForTag(mFont, tag);
    // Bug 1602391 suggests CGFontCopyTableForTag can fail, even though we just
    // got the tag from the font via CGFontCopyTableTags above. If we can catch
    // this (e.g. in fuzz-testing) it'd be good to understand when it happens,
    // but in any case we'll handle it safely below by treating the table as
    // zero-length.
    MOZ_ASSERT(data, "failed to get font table data");
    records[i].tag = tag;
    records[i].offset = offset;
    records[i].data = data;
    if (data) {
      records[i].length = CFDataGetLength(data);
      bool skipChecksumAdjust = (tag == 0x68656164);  // 'head'
      records[i].checkSum = CalcTableChecksum(
          reinterpret_cast<const uint32_t*>(CFDataGetBytePtr(data)),
          records[i].length, skipChecksumAdjust);
      offset += records[i].length;
      // 32 bit align the tables
      offset = (offset + 3) & ~3;
    } else {
      records[i].length = 0;
      records[i].checkSum = 0;
    }
  }
  CFRelease(tags);

  struct writeBuf buf(offset);
  // write header/offset table
  if (CFF) {
    buf.writeElement(CFSwapInt32HostToBig(0x4f54544f));
  } else {
    buf.writeElement(CFSwapInt32HostToBig(0x00010000));
  }
  buf.writeElement(CFSwapInt16HostToBig(count));
  int maxPow2Count = maxPow2LessThanEqual(count);
  buf.writeElement(CFSwapInt16HostToBig((1 << maxPow2Count) * 16));
  buf.writeElement(CFSwapInt16HostToBig(maxPow2Count));
  buf.writeElement(CFSwapInt16HostToBig((count - (1 << maxPow2Count)) * 16));

  // write table record entries
  for (CFIndex i = 0; i < count; i++) {
    buf.writeElement(CFSwapInt32HostToBig(records[i].tag));
    buf.writeElement(CFSwapInt32HostToBig(records[i].checkSum));
    buf.writeElement(CFSwapInt32HostToBig(records[i].offset));
    buf.writeElement(CFSwapInt32HostToBig(records[i].length));
  }

  // write tables
  int checkSumAdjustmentOffset = 0;
  for (CFIndex i = 0; i < count; i++) {
    if (records[i].tag == 0x68656164) {
      checkSumAdjustmentOffset = buf.offset + 2 * 4;
    }
    if (records[i].data) {
      buf.writeMem(CFDataGetBytePtr(records[i].data), records[i].length);
      buf.align();
      CFRelease(records[i].data);
    }
  }
  delete[] records;

  // clear the checksumAdjust field before checksumming the whole font
  memset(&buf.data[checkSumAdjustmentOffset], 0, sizeof(uint32_t));
  uint32_t fontChecksum = CFSwapInt32HostToBig(
      0xb1b0afba -
      CalcTableChecksum(reinterpret_cast<const uint32_t*>(buf.data), offset));
  // set checkSumAdjust to the computed checksum
  memcpy(&buf.data[checkSumAdjustmentOffset], &fontChecksum,
         sizeof(fontChecksum));

  // we always use an index of 0
  return aDataCallback(buf.data, buf.offset, 0, aBaton);
}

bool UnscaledFontMac::GetFontDescriptor(FontDescriptorOutput aCb,
                                        void* aBaton) {
  if (mIsDataFont) {
    return false;
  }

  AutoRelease<CFStringRef> psname(CGFontCopyPostScriptName(mFont));
  if (!psname) {
    return false;
  }

  char buf[1024];
  const char* cstr = CFStringGetCStringPtr(psname, kCFStringEncodingUTF8);
  if (!cstr) {
    if (!CFStringGetCString(psname, buf, sizeof(buf), kCFStringEncodingUTF8)) {
      return false;
    }
    cstr = buf;
  }

  nsAutoCString descriptor(cstr);
  uint32_t psNameLen = descriptor.Length();

  AutoRelease<CTFontRef> ctFont(
      CTFontCreateWithGraphicsFont(mFont, 0, nullptr, nullptr));
  AutoRelease<CFURLRef> fontUrl(
      (CFURLRef)CTFontCopyAttribute(ctFont, kCTFontURLAttribute));
  if (fontUrl) {
    CFStringRef urlStr(CFURLCopyFileSystemPath(fontUrl, kCFURLPOSIXPathStyle));
    cstr = CFStringGetCStringPtr(urlStr, kCFStringEncodingUTF8);
    if (!cstr) {
      if (!CFStringGetCString(urlStr, buf, sizeof(buf),
                              kCFStringEncodingUTF8)) {
        return false;
      }
      cstr = buf;
    }
    descriptor.Append(cstr);
  }

  return aCb(reinterpret_cast<const uint8_t*>(descriptor.get()),
             descriptor.Length(), psNameLen, aBaton);
}

static void CollectVariationsFromDictionary(const void* aKey,
                                            const void* aValue,
                                            void* aContext) {
  auto keyPtr = static_cast<const CFTypeRef>(aKey);
  auto valuePtr = static_cast<const CFTypeRef>(aValue);
  auto outVariations = static_cast<std::vector<wr::FontVariation>*>(aContext);
  if (CFGetTypeID(keyPtr) == CFNumberGetTypeID() &&
      CFGetTypeID(valuePtr) == CFNumberGetTypeID()) {
    uint64_t t;
    double v;
    if (CFNumberGetValue(static_cast<CFNumberRef>(keyPtr), kCFNumberSInt64Type,
                         &t) &&
        CFNumberGetValue(static_cast<CFNumberRef>(valuePtr),
                         kCFNumberDoubleType, &v)) {
      outVariations->push_back(wr::FontVariation{uint32_t(t), float(v)});
    }
  }
}

static bool GetVariationsForCTFont(
    CTFontRef aCTFont, std::vector<wr::FontVariation>* aOutVariations) {
  if (!aCTFont) {
    return true;
  }
  AutoRelease<CFDictionaryRef> dict(CTFontCopyVariation(aCTFont));
  CFIndex count = dict ? CFDictionaryGetCount(dict) : 0;
  if (count > 0) {
    aOutVariations->reserve(count);
    CFDictionaryApplyFunction(dict, CollectVariationsFromDictionary,
                              aOutVariations);
  }
  return true;
}

bool ScaledFontMac::GetFontInstanceData(FontInstanceDataOutput aCb,
                                        void* aBaton) {
  // Collect any variation settings that were incorporated into the CTFont.
  std::vector<wr::FontVariation> variations;
  if (!GetVariationsForCTFont(mCTFont, &variations)) {
    return false;
  }

  InstanceData instance(this);
  return aCb(reinterpret_cast<uint8_t*>(&instance), sizeof(instance),
             variations.data(), variations.size(), aBaton);
}

bool ScaledFontMac::GetWRFontInstanceOptions(
    Maybe<wr::FontInstanceOptions>* aOutOptions,
    Maybe<wr::FontInstancePlatformOptions>* aOutPlatformOptions,
    std::vector<wr::FontVariation>* aOutVariations) {
  GetVariationsForCTFont(mCTFont, aOutVariations);

  wr::FontInstanceOptions options = {};
  options.render_mode = wr::FontRenderMode::Subpixel;
  options.flags = wr::FontInstanceFlags::SUBPIXEL_POSITION;
  if (mUseFontSmoothing) {
    options.flags |= wr::FontInstanceFlags::FONT_SMOOTHING;
  }
  if (mApplySyntheticBold) {
    options.flags |= wr::FontInstanceFlags::SYNTHETIC_BOLD;
  }
  if (mHasColorGlyphs) {
    options.flags |= wr::FontInstanceFlags::EMBEDDED_BITMAPS;
  }
  options.synthetic_italics =
      wr::DegreesToSyntheticItalics(GetSyntheticObliqueAngle());
  *aOutOptions = Some(options);
  return true;
}

ScaledFontMac::InstanceData::InstanceData(
    const wr::FontInstanceOptions* aOptions,
    const wr::FontInstancePlatformOptions* aPlatformOptions)
    : mUseFontSmoothing(true),
      mApplySyntheticBold(false),
      mHasColorGlyphs(false) {
  if (aOptions) {
    if (!(aOptions->flags & wr::FontInstanceFlags::FONT_SMOOTHING)) {
      mUseFontSmoothing = false;
    }
    if (aOptions->flags & wr::FontInstanceFlags::SYNTHETIC_BOLD) {
      mApplySyntheticBold = true;
    }
    if (aOptions->flags & wr::FontInstanceFlags::EMBEDDED_BITMAPS) {
      mHasColorGlyphs = true;
    }
  }
}

static CFDictionaryRef CreateVariationDictionaryOrNull(
    CGFontRef aCGFont, CFArrayRef& aCGAxesCache, CFArrayRef& aCTAxesCache,
    uint32_t aVariationCount, const wr::FontVariation* aVariations) {
  if (!aCGAxesCache) {
    aCGAxesCache = CGFontCopyVariationAxes(aCGFont);
    if (!aCGAxesCache) {
      return nullptr;
    }
  }
  if (!aCTAxesCache) {
    AutoRelease<CTFontRef> ctFont(
        CTFontCreateWithGraphicsFont(aCGFont, 0, nullptr, nullptr));
    aCTAxesCache = CTFontCopyVariationAxes(ctFont);
    if (!aCTAxesCache) {
      return nullptr;
    }
  }

  CFIndex axisCount = CFArrayGetCount(aCTAxesCache);
  if (CFArrayGetCount(aCGAxesCache) != axisCount) {
    return nullptr;
  }

  AutoRelease<CFMutableDictionaryRef> dict(CFDictionaryCreateMutable(
      kCFAllocatorDefault, axisCount, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks));

  // Number of variation settings passed in the aVariations parameter.
  // This will typically be a very low value, so we just linear-search them.
  bool allDefaultValues = true;

  for (CFIndex i = 0; i < axisCount; ++i) {
    // We sanity-check the axis info found in the CTFont, and bail out
    // (returning null) if it doesn't have the expected types.
    CFTypeRef axisInfo = CFArrayGetValueAtIndex(aCTAxesCache, i);
    if (CFDictionaryGetTypeID() != CFGetTypeID(axisInfo)) {
      return nullptr;
    }
    CFDictionaryRef axis = static_cast<CFDictionaryRef>(axisInfo);

    CFTypeRef axisTag =
        CFDictionaryGetValue(axis, kCTFontVariationAxisIdentifierKey);
    if (!axisTag || CFGetTypeID(axisTag) != CFNumberGetTypeID()) {
      return nullptr;
    }
    int64_t tagLong;
    if (!CFNumberGetValue(static_cast<CFNumberRef>(axisTag),
                          kCFNumberSInt64Type, &tagLong)) {
      return nullptr;
    }

    axisInfo = CFArrayGetValueAtIndex(aCGAxesCache, i);
    if (CFDictionaryGetTypeID() != CFGetTypeID(axisInfo)) {
      return nullptr;
    }
    CFTypeRef axisName = CFDictionaryGetValue(
        static_cast<CFDictionaryRef>(axisInfo), kCGFontVariationAxisName);
    if (!axisName || CFGetTypeID(axisName) != CFStringGetTypeID()) {
      return nullptr;
    }

    // Clamp axis values to the supported range.
    CFTypeRef min =
        CFDictionaryGetValue(axis, kCTFontVariationAxisMinimumValueKey);
    CFTypeRef max =
        CFDictionaryGetValue(axis, kCTFontVariationAxisMaximumValueKey);
    CFTypeRef def =
        CFDictionaryGetValue(axis, kCTFontVariationAxisDefaultValueKey);
    if (!min || CFGetTypeID(min) != CFNumberGetTypeID() || !max ||
        CFGetTypeID(max) != CFNumberGetTypeID() || !def ||
        CFGetTypeID(def) != CFNumberGetTypeID()) {
      return nullptr;
    }
    double minDouble;
    double maxDouble;
    double defDouble;
    if (!CFNumberGetValue(static_cast<CFNumberRef>(min), kCFNumberDoubleType,
                          &minDouble) ||
        !CFNumberGetValue(static_cast<CFNumberRef>(max), kCFNumberDoubleType,
                          &maxDouble) ||
        !CFNumberGetValue(static_cast<CFNumberRef>(def), kCFNumberDoubleType,
                          &defDouble)) {
      return nullptr;
    }

    double value = defDouble;
    for (uint32_t j = 0; j < aVariationCount; ++j) {
      if (aVariations[j].tag == tagLong) {
        value = std::clamp<double>(aVariations[j].value, minDouble, maxDouble);
        if (value != defDouble) {
          allDefaultValues = false;
        }
        break;
      }
    }
    AutoRelease<CFNumberRef> valueNumber(
        CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &value));
    CFDictionaryAddValue(dict, axisName, valueNumber);
  }

  if (allDefaultValues) {
    // We didn't actually set any non-default values, so throw away the
    // variations dictionary and just use the default rendering.
    return nullptr;
  }

  return dict.forget();
}

static CFDictionaryRef CreateVariationTagDictionaryOrNull(
    CTFontRef aCTFont, uint32_t aVariationCount,
    const wr::FontVariation* aVariations) {
  AutoRelease<CFArrayRef> axes(CTFontCopyVariationAxes(aCTFont));
  CFIndex axisCount = CFArrayGetCount(axes);

  AutoRelease<CFMutableDictionaryRef> dict(CFDictionaryCreateMutable(
      kCFAllocatorDefault, axisCount, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks));

  // Number of variation settings passed in the aVariations parameter.
  // This will typically be a very low value, so we just linear-search them.
  bool allDefaultValues = true;

  for (CFIndex i = 0; i < axisCount; ++i) {
    // We sanity-check the axis info found in the CTFont, and bail out
    // (returning null) if it doesn't have the expected types.
    CFTypeRef axisInfo = CFArrayGetValueAtIndex(axes, i);
    if (CFDictionaryGetTypeID() != CFGetTypeID(axisInfo)) {
      return nullptr;
    }
    CFDictionaryRef axis = static_cast<CFDictionaryRef>(axisInfo);

    CFTypeRef axisTag =
        CFDictionaryGetValue(axis, kCTFontVariationAxisIdentifierKey);
    if (!axisTag || CFGetTypeID(axisTag) != CFNumberGetTypeID()) {
      return nullptr;
    }
    int64_t tagLong;
    if (!CFNumberGetValue(static_cast<CFNumberRef>(axisTag),
                          kCFNumberSInt64Type, &tagLong)) {
      return nullptr;
    }

    // Clamp axis values to the supported range.
    CFTypeRef min =
        CFDictionaryGetValue(axis, kCTFontVariationAxisMinimumValueKey);
    CFTypeRef max =
        CFDictionaryGetValue(axis, kCTFontVariationAxisMaximumValueKey);
    CFTypeRef def =
        CFDictionaryGetValue(axis, kCTFontVariationAxisDefaultValueKey);
    if (!min || CFGetTypeID(min) != CFNumberGetTypeID() || !max ||
        CFGetTypeID(max) != CFNumberGetTypeID() || !def ||
        CFGetTypeID(def) != CFNumberGetTypeID()) {
      return nullptr;
    }
    double minDouble;
    double maxDouble;
    double defDouble;
    if (!CFNumberGetValue(static_cast<CFNumberRef>(min), kCFNumberDoubleType,
                          &minDouble) ||
        !CFNumberGetValue(static_cast<CFNumberRef>(max), kCFNumberDoubleType,
                          &maxDouble) ||
        !CFNumberGetValue(static_cast<CFNumberRef>(def), kCFNumberDoubleType,
                          &defDouble)) {
      return nullptr;
    }

    double value = defDouble;
    for (uint32_t j = 0; j < aVariationCount; ++j) {
      if (aVariations[j].tag == tagLong) {
        value = std::clamp<double>(aVariations[j].value, minDouble, maxDouble);
        if (value != defDouble) {
          allDefaultValues = false;
        }
        break;
      }
    }
    AutoRelease<CFNumberRef> valueNumber(
        CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &value));
    CFDictionaryAddValue(dict, axisTag, valueNumber);
  }

  if (allDefaultValues) {
    // We didn't actually set any non-default values, so throw away the
    // variations dictionary and just use the default rendering.
    return nullptr;
  }

  return dict.forget();
}

/* static */
CGFontRef UnscaledFontMac::CreateCGFontWithVariations(
    CGFontRef aFont, CFArrayRef& aCGAxesCache, CFArrayRef& aCTAxesCache,
    uint32_t aVariationCount, const wr::FontVariation* aVariations) {
  if (!aVariationCount) {
    return nullptr;
  }
  MOZ_ASSERT(aVariations);

  AutoRelease<CFDictionaryRef> varDict(CreateVariationDictionaryOrNull(
      aFont, aCGAxesCache, aCTAxesCache, aVariationCount, aVariations));
  if (!varDict) {
    return nullptr;
  }

  return CGFontCreateCopyWithVariations(aFont, varDict);
}

already_AddRefed<ScaledFont> UnscaledFontMac::CreateScaledFont(
    Float aGlyphSize, const uint8_t* aInstanceData,
    uint32_t aInstanceDataLength, const wr::FontVariation* aVariations,
    uint32_t aNumVariations)

{
  if (aInstanceDataLength < sizeof(ScaledFontMac::InstanceData)) {
    gfxWarning() << "Mac scaled font instance data is truncated.";
    return nullptr;
  }
  const ScaledFontMac::InstanceData& instanceData =
      *reinterpret_cast<const ScaledFontMac::InstanceData*>(aInstanceData);
  RefPtr<ScaledFontMac> scaledFont;
  if (mFontDesc) {
    AutoRelease<CTFontRef> font(
        CTFontCreateWithFontDescriptor(mFontDesc, aGlyphSize, nullptr));
    if (aNumVariations > 0) {
      AutoRelease<CFDictionaryRef> varDict(CreateVariationTagDictionaryOrNull(
          font, aNumVariations, aVariations));
      if (varDict) {
        CFDictionaryRef varAttr = CFDictionaryCreate(
            nullptr, (const void**)&kCTFontVariationAttribute,
            (const void**)&varDict, 1, &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        AutoRelease<CTFontDescriptorRef> fontDesc(
            CTFontDescriptorCreateCopyWithAttributes(mFontDesc, varAttr));
        if (!fontDesc) {
          return nullptr;
        }
        font = CTFontCreateWithFontDescriptor(fontDesc, aGlyphSize, nullptr);
      }
    }
    scaledFont = new ScaledFontMac(font, this, instanceData.mUseFontSmoothing,
                                   instanceData.mApplySyntheticBold,
                                   instanceData.mHasColorGlyphs);
  } else {
    CGFontRef fontRef = mFont;
    if (aNumVariations > 0) {
      CGFontRef varFont = CreateCGFontWithVariations(
          mFont, mCGAxesCache, mCTAxesCache, aNumVariations, aVariations);
      if (varFont) {
        fontRef = varFont;
      }
    }

    scaledFont = new ScaledFontMac(fontRef, this, aGlyphSize, fontRef != mFont,
                                   instanceData.mUseFontSmoothing,
                                   instanceData.mApplySyntheticBold,
                                   instanceData.mHasColorGlyphs);
  }
  return scaledFont.forget();
}

already_AddRefed<ScaledFont> UnscaledFontMac::CreateScaledFontFromWRFont(
    Float aGlyphSize, const wr::FontInstanceOptions* aOptions,
    const wr::FontInstancePlatformOptions* aPlatformOptions,
    const wr::FontVariation* aVariations, uint32_t aNumVariations) {
  ScaledFontMac::InstanceData instanceData(aOptions, aPlatformOptions);
  return CreateScaledFont(aGlyphSize, reinterpret_cast<uint8_t*>(&instanceData),
                          sizeof(instanceData), aVariations, aNumVariations);
}

cairo_font_face_t* ScaledFontMac::CreateCairoFontFace(
    cairo_font_options_t* aFontOptions) {
  MOZ_ASSERT(mFont);
  return cairo_quartz_font_face_create_for_cgfont(mFont);
}

already_AddRefed<UnscaledFont> UnscaledFontMac::CreateFromFontDescriptor(
    const uint8_t* aData, uint32_t aDataLength, uint32_t aIndex) {
  if (aDataLength == 0 || aIndex > aDataLength) {
    gfxWarning() << "Mac font descriptor is truncated.";
    return nullptr;
  }
  AutoRelease<CFStringRef> name(
      CFStringCreateWithBytes(kCFAllocatorDefault, (const UInt8*)aData, aIndex,
                              kCFStringEncodingUTF8, false));
  if (!name) {
    return nullptr;
  }
  CGFontRef font = CGFontCreateWithFontName(name);
  if (!font) {
    return nullptr;
  }

  // If the descriptor included a font file path, apply that attribute and
  // refresh the font in case it changed.
  if (aIndex < aDataLength) {
    AutoRelease<CFStringRef> path(CFStringCreateWithBytes(
        kCFAllocatorDefault, (const UInt8*)aData + aIndex, aDataLength - aIndex,
        kCFStringEncodingUTF8, false));
    AutoRelease<CFURLRef> url(CFURLCreateWithFileSystemPath(
        kCFAllocatorDefault, path, kCFURLPOSIXPathStyle, false));
    AutoRelease<CFDictionaryRef> attrs(CFDictionaryCreate(
        nullptr, (const void**)&kCTFontURLAttribute, (const void**)&url, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
    AutoRelease<CTFontRef> ctFont(
        CTFontCreateWithGraphicsFont(font, 0.0, nullptr, nullptr));
    AutoRelease<CTFontDescriptorRef> desc(CTFontCopyFontDescriptor(ctFont));
    AutoRelease<CTFontDescriptorRef> newDesc(
        CTFontDescriptorCreateCopyWithAttributes(desc, attrs));
    AutoRelease<CTFontRef> newFont(
        CTFontCreateWithFontDescriptor(newDesc, 0.0, nullptr));
    CFRelease(font);
    font = CTFontCopyGraphicsFont(newFont, nullptr);
  }

  RefPtr unscaledFont = MakeRefPtr<UnscaledFontMac>(font);
  CFRelease(font);
  return unscaledFont.forget();
}

}  // namespace gfx
}  // namespace mozilla
