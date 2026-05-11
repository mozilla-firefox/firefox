/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <unordered_map>
#include "NativeFontResourceMac.h"
#include "UnscaledFontMac.h"
#include "Types.h"

#include "mozilla/RefPtr.h"
#include "mozilla/DataMutex.h"

#ifdef MOZ_WIDGET_UIKIT
#  include <CoreFoundation/CoreFoundation.h>
#endif

#include "nsIMemoryReporter.h"

namespace mozilla {
namespace gfx {

#define FONT_NAME_MAX 32
MOZ_RUNINIT static StaticDataMutex<
    std::unordered_map<void*, nsAutoCStringN<FONT_NAME_MAX>>>
    sWeakFontDataMap("WeakFonts");

void FontDataDeallocate(void*, void* info) {
  auto fontMap = sWeakFontDataMap.Lock();
  fontMap->erase(info);
  free(info);
}

class NativeFontResourceMacReporter final : public nsIMemoryReporter {
  ~NativeFontResourceMacReporter() = default;

  MOZ_DEFINE_MALLOC_SIZE_OF(MallocSizeOf)
 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override {
    auto fontMap = sWeakFontDataMap.Lock();

    nsAutoCString path("explicit/gfx/native-font-resource-mac/font(");

    unsigned int unknownFontIndex = 0;
    for (auto& i : *fontMap) {
      nsAutoCString subPath(path);

      if (aAnonymize) {
        subPath.AppendPrintf("<anonymized-%p>", this);
      } else {
        if (i.second.Length()) {
          subPath.AppendLiteral("psname=");
          subPath.Append(i.second);
        } else {
          subPath.AppendPrintf("Unknown(%d)", unknownFontIndex);
        }
      }

      size_t bytes = MallocSizeOf(i.first) + FONT_NAME_MAX;

      subPath.Append(")");

      aHandleReport->Callback(""_ns, subPath, KIND_HEAP, UNITS_BYTES, bytes,
                              "Memory used by this native font."_ns, aData);

      unknownFontIndex++;
    }
    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(NativeFontResourceMacReporter, nsIMemoryReporter)

void NativeFontResourceMac::RegisterMemoryReporter() {
  RegisterStrongMemoryReporter(MakeAndAddRef<NativeFontResourceMacReporter>());
}

/* static */
already_AddRefed<NativeFontResourceMac> NativeFontResourceMac::Create(
    const uint8_t* aFontData, uint32_t aDataLength) {
  uint8_t* fontData = (uint8_t*)malloc(aDataLength);
  if (!fontData) {
    return nullptr;
  }
  memcpy(fontData, aFontData, aDataLength);
  CFAllocatorContext context = {0,       fontData, nullptr, nullptr,
                                nullptr, nullptr,  nullptr, FontDataDeallocate,
                                nullptr};
  CFAllocatorRef allocator = CFAllocatorCreate(kCFAllocatorDefault, &context);

  // We create a CFDataRef here that we'l hold until we've determined that we
  // have a valid font. If and only if we can create a font from the data,
  // we'll store the font data in our map. Whether or not the font is valid,
  // we'll later release this CFDataRef.
  CFDataRef data = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, fontData,
                                               aDataLength, allocator);
  if (!data) {
    free(fontData);
    return nullptr;
  }

  CTFontDescriptorRef ctFontDesc =
      CTFontManagerCreateFontDescriptorFromData(data);
  if (!ctFontDesc) {
    CFRelease(data);
    return nullptr;
  }

  // creating the CGFontRef via the CTFont avoids the data being held alive
  // in a cache.
  CTFontRef ctFont = CTFontCreateWithFontDescriptor(ctFontDesc, 0, NULL);

  // Creating the CGFont from the CTFont prevents the font data from being
  // held in the TDescriptorSource cache. This appears to be true even
  // if we later create a CTFont from the CGFont.
  CGFontRef fontRef = CTFontCopyGraphicsFont(ctFont, NULL);
  CFRelease(ctFont);

  if (!fontRef) {
    // Not a valid font; release the structures we've been holding.
    CFRelease(data);
    CFRelease(ctFontDesc);
    return nullptr;
  }

  // Determine the font name and store it with the font data in the map.
  nsAutoCStringN<FONT_NAME_MAX> fontName;

  CFStringRef psname = CGFontCopyPostScriptName(fontRef);
  if (psname) {
    const char* cstr = CFStringGetCStringPtr(psname, kCFStringEncodingUTF8);
    if (cstr) {
      fontName.Assign(cstr);
    } else {
      char buf[FONT_NAME_MAX];
      if (CFStringGetCString(psname, buf, FONT_NAME_MAX,
                             kCFStringEncodingUTF8)) {
        fontName.Assign(buf);
      }
    }
    CFRelease(psname);
  }

  {
    auto fontMap = sWeakFontDataMap.Lock();
    void* key = (void*)fontData;
    fontMap->insert({key, fontName});
  }
  // It's now safe to release our CFDataRef.
  CFRelease(data);

  // passes ownership of fontRef to the NativeFontResourceMac instance
  RefPtr<NativeFontResourceMac> fontResource =
      new NativeFontResourceMac(ctFontDesc, fontRef, aDataLength);

  return fontResource.forget();
}

already_AddRefed<UnscaledFont> NativeFontResourceMac::CreateUnscaledFont(
    uint32_t aIndex, const uint8_t* aInstanceData,
    uint32_t aInstanceDataLength) {
  RefPtr<UnscaledFont> unscaledFont =
      new UnscaledFontMac(mFontDescRef, mFontRef, true);

  return unscaledFont.forget();
}

}  // namespace gfx
}  // namespace mozilla
