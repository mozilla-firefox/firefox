/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ScaledFontDWrite.h"

#include <vector>

#include "HelpersWinFonts.h"
#include "Logging.h"
#include "PathSkia.h"
#include "StackArray.h"
#include "UnscaledFontDWrite.h"
#include "cairo-dwrite.h"
#include "dwrite_3.h"
#include "gfxDWriteCommon.h"
#include "gfxFont.h"
#include "mozilla/FontPropertyTypes.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "skia/include/core/SkPaint.h"
#include "skia/include/core/SkPath.h"
#include "skia/include/ports/SkTypeface_win.h"

namespace mozilla {
namespace gfx {

#define GASP_TAG 0x70736167
#define GASP_DOGRAY 0x2

static inline unsigned short readShort(const char* aBuf) {
  return (*aBuf << 8) | *(aBuf + 1);
}

static bool DoGrayscale(IDWriteFontFace* aDWFace, Float ppem) {
  void* tableContext;
  char* tableData;
  UINT32 tableSize;
  BOOL exists;
  aDWFace->TryGetFontTable(GASP_TAG, (const void**)&tableData, &tableSize,
                           &tableContext, &exists);

  if (exists) {
    if (tableSize < 4) {
      aDWFace->ReleaseFontTable(tableContext);
      return true;
    }
    struct gaspRange {
      unsigned short maxPPEM;   // Stored big-endian
      unsigned short behavior;  // Stored big-endian
    };
    unsigned short numRanges = readShort(tableData + 2);
    if (tableSize < (UINT)4 + numRanges * 4) {
      aDWFace->ReleaseFontTable(tableContext);
      return true;
    }
    gaspRange* ranges = (gaspRange*)(tableData + 4);
    for (int i = 0; i < numRanges; i++) {
      if (readShort((char*)&ranges[i].maxPPEM) >= ppem) {
        if (!(readShort((char*)&ranges[i].behavior) & GASP_DOGRAY)) {
          aDWFace->ReleaseFontTable(tableContext);
          return false;
        }
        break;
      }
    }
    aDWFace->ReleaseFontTable(tableContext);
  }
  return true;
}

ScaledFontDWrite::ScaledFontDWrite(IDWriteFontFace* aFontFace,
                                   const RefPtr<UnscaledFont>& aUnscaledFont,
                                   Float aSize, bool aUseEmbeddedBitmap,
                                   bool aUseMultistrikeBold, bool aGDIForced,
                                   const gfxFontStyle* aStyle)
    : ScaledFontBase(aUnscaledFont, aSize),
      mFontFace(aFontFace),
      mUseEmbeddedBitmap(aUseEmbeddedBitmap),
      mUseMultistrikeBold(aUseMultistrikeBold),
      mGDIForced(aGDIForced) {
  if (aStyle) {
    mStyle = SkFontStyle(aStyle->weight.ToIntRounded(),
                         DWriteFontStretchFromWidth(aStyle->width),
                         // FIXME(jwatt): also use kOblique_Slant
                         aStyle->style == FontSlantStyle::NORMAL
                             ? SkFontStyle::kUpright_Slant
                             : SkFontStyle::kItalic_Slant);
  }
}

SkTypeface* ScaledFontDWrite::CreateSkTypeface() {
  RefPtr<IDWriteFactory> factory = Factory::GetDWriteFactory();
  if (!factory) {
    return nullptr;
  }

  auto& settings = DWriteSettings();
  Float gamma = settings.Gamma();
  // Skia doesn't support a gamma value outside of 0-4, so default to 2.2
  if (gamma < 0.0f || gamma > 4.0f) {
    gamma = 2.2f;
  }

  Float contrast = settings.EnhancedContrast();
  // Skia doesn't support a contrast value outside of 0-1, so default to 1.0
  if (contrast < 0.0f || contrast > 1.0f) {
    contrast = 1.0f;
  }

  Float clearTypeLevel = settings.ClearTypeLevel();
  if (clearTypeLevel < 0.0f || clearTypeLevel > 1.0f) {
    clearTypeLevel = 1.0f;
  }

  IDWriteFont* font =
      static_cast<UnscaledFontDWrite*>(mUnscaledFont.get())->GetFont();
  RefPtr<IDWriteFontFamily> family;
  if (font) {
    font->GetFontFamily(getter_AddRefs(family));
  }
  return SkCreateTypefaceFromDWriteFont(factory, mFontFace, font, family,
                                        mStyle, (int)settings.RenderingMode(),
                                        gamma, contrast, clearTypeLevel);
}

void ScaledFontDWrite::SetupSkFontDrawOptions(SkFont& aFont) {
  if (ForceGDIMode()) {
    aFont.setEmbeddedBitmaps(true);
    aFont.setSubpixel(false);
  } else {
    aFont.setEmbeddedBitmaps(UseEmbeddedBitmaps());
    aFont.setSubpixel(true);
  }
}

bool ScaledFontDWrite::MayUseBitmaps() {
  return ForceGDIMode() || UseEmbeddedBitmaps();
}

bool UnscaledFontDWrite::GetFontFileData(FontFileDataOutput aDataCallback,
                                         void* aBaton) {
  UINT32 fileCount = 0;
  HRESULT hr = mFontFace->GetFiles(&fileCount, nullptr);

  if (FAILED(hr) || fileCount > 1) {
    MOZ_ASSERT(false);
    return false;
  }

  if (!aDataCallback) {
    return true;
  }

  RefPtr<IDWriteFontFile> file;
  hr = mFontFace->GetFiles(&fileCount, getter_AddRefs(file));
  if (FAILED(hr)) {
    return false;
  }

  const void* referenceKey;
  UINT32 refKeySize;
  // XXX - This can currently crash for webfonts, as when we get the reference
  // key out of the file, that can be an invalid reference key for the loader
  // we use it with. The fix to this is not obvious but it will probably
  // have to happen inside thebes.
  hr = file->GetReferenceKey(&referenceKey, &refKeySize);
  if (FAILED(hr)) {
    return false;
  }

  RefPtr<IDWriteFontFileLoader> loader;
  hr = file->GetLoader(getter_AddRefs(loader));
  if (FAILED(hr)) {
    return false;
  }

  RefPtr<IDWriteFontFileStream> stream;
  hr = loader->CreateStreamFromKey(referenceKey, refKeySize,
                                   getter_AddRefs(stream));
  if (FAILED(hr)) {
    return false;
  }

  UINT64 fileSize64;
  hr = stream->GetFileSize(&fileSize64);
  if (FAILED(hr) || fileSize64 > UINT32_MAX) {
    MOZ_ASSERT(false);
    return false;
  }

  // Try to catch any device memory exceptions that may occur while attempting
  // to read the file fragment.
  void* context = nullptr;
  bool success = false;
  hr = E_FAIL;
  MOZ_SEH_TRY {
    uint32_t fileSize = static_cast<uint32_t>(fileSize64);
    const void* fragmentStart = nullptr;
    hr = stream->ReadFileFragment(&fragmentStart, 0, fileSize, &context);
    if (SUCCEEDED(hr)) {
      success = aDataCallback((uint8_t*)fragmentStart, fileSize,
                              mFontFace->GetIndex(), aBaton);
    }
  }
  MOZ_SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
    gfxCriticalNote << "Exception occurred reading DWrite font file data";
  }
  if (FAILED(hr)) {
    return false;
  }
  stream->ReleaseFileFragment(context);
  return success;
}

static bool GetFontFileName(RefPtr<IDWriteFontFace> aFontFace,
                            std::vector<WCHAR>& aFileName) {
  UINT32 numFiles;
  HRESULT hr = aFontFace->GetFiles(&numFiles, nullptr);
  if (FAILED(hr)) {
    gfxDebug() << "Failed getting file count for WR font";
    return false;
  } else if (numFiles != 1) {
    gfxDebug() << "Invalid file count " << numFiles << " for WR font";
    return false;
  }

  RefPtr<IDWriteFontFile> file;
  hr = aFontFace->GetFiles(&numFiles, getter_AddRefs(file));
  if (FAILED(hr)) {
    gfxDebug() << "Failed getting file for WR font";
    return false;
  }

  const void* key;
  UINT32 keySize;
  hr = file->GetReferenceKey(&key, &keySize);
  if (FAILED(hr)) {
    gfxDebug() << "Failed getting file ref key for WR font";
    return false;
  }
  RefPtr<IDWriteFontFileLoader> loader;
  hr = file->GetLoader(getter_AddRefs(loader));
  if (FAILED(hr)) {
    gfxDebug() << "Failed getting file loader for WR font";
    return false;
  }
  RefPtr<IDWriteLocalFontFileLoader> localLoader;
  loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader),
                         (void**)getter_AddRefs(localLoader));
  if (!localLoader) {
    gfxDebug() << "Failed querying loader interface for WR font";
    return false;
  }
  UINT32 pathLen;
  hr = localLoader->GetFilePathLengthFromKey(key, keySize, &pathLen);
  if (FAILED(hr)) {
    gfxDebug() << "Failed getting path length for WR font";
    return false;
  }
  aFileName.resize(pathLen + 1);
  hr = localLoader->GetFilePathFromKey(key, keySize, aFileName.data(),
                                       pathLen + 1);
  if (FAILED(hr) || aFileName.back() != 0) {
    gfxDebug() << "Failed getting path for WR font";
    return false;
  }

  // We leave the null terminator at the end of the returned file name.
  return true;
}

bool UnscaledFontDWrite::GetFontDescriptor(FontDescriptorOutput aCb,
                                           void* aBaton) {
  MOZ_ASSERT(NS_IsMainThread());

  // We cache the font file name as it involves multiple DirectWrite calls.
  if (mFontFileName.empty()) {
    if (!mFont) {
      return false;
    }

    if (!GetFontFileName(mFontFace, mFontFileName)) {
      return false;
    }
  }

  uint32_t index = mFontFace->GetIndex();
  return aCb(reinterpret_cast<const uint8_t*>(mFontFileName.data()),
             mFontFileName.size() * sizeof(WCHAR), index, aBaton);
}

ScaledFontDWrite::InstanceData::InstanceData(
    const wr::FontInstanceOptions* aOptions,
    const wr::FontInstancePlatformOptions* aPlatformOptions) {
  if (aOptions) {
    if (aOptions->flags & wr::FontInstanceFlags::EMBEDDED_BITMAPS) {
      mUseEmbeddedBitmap = true;
    }
    if (aOptions->flags & wr::FontInstanceFlags::SYNTHETIC_BOLD) {
      mUseBoldSimulation = true;
    }
    if (aOptions->flags & wr::FontInstanceFlags::MULTISTRIKE_BOLD) {
      mUseMultistrikeBold = true;
    }
    if (aOptions->flags & wr::FontInstanceFlags::FORCE_GDI) {
      mGDIForced = true;
    }
  }
}

bool ScaledFontDWrite::HasVariationSettings() {
  RefPtr<IDWriteFontFace5> ff5;
  mFontFace->QueryInterface(__uuidof(IDWriteFontFace5),
                            (void**)getter_AddRefs(ff5));
  if (!ff5 || !ff5->HasVariations()) {
    return false;
  }

  uint32_t count = ff5->GetFontAxisValueCount();
  if (!count) {
    return false;
  }

  RefPtr<IDWriteFontResource> res;
  if (FAILED(ff5->GetFontResource(getter_AddRefs(res)))) {
    return false;
  }

  std::vector<DWRITE_FONT_AXIS_VALUE> defaults(count);
  if (FAILED(res->GetDefaultFontAxisValues(defaults.data(), count))) {
    return false;
  }

  std::vector<DWRITE_FONT_AXIS_VALUE> values(count);
  if (FAILED(ff5->GetFontAxisValues(values.data(), count))) {
    return false;
  }

  for (uint32_t i = 0; i < count; i++) {
    DWRITE_FONT_AXIS_ATTRIBUTES attr = res->GetFontAxisAttributes(i);
    if (attr & DWRITE_FONT_AXIS_ATTRIBUTES_VARIABLE) {
      if (values[i].value != defaults[i].value) {
        return true;
      }
    }
  }

  return false;
}

// Helper for ScaledFontDWrite::GetFontInstanceData: if the font has variation
// axes, get their current values into the aOutput vector.
static void GetVariationsFromFontFace(IDWriteFontFace* aFace,
                                      std::vector<wr::FontVariation>* aOutput) {
  RefPtr<IDWriteFontFace5> ff5;
  aFace->QueryInterface(__uuidof(IDWriteFontFace5),
                        (void**)getter_AddRefs(ff5));
  if (!ff5 || !ff5->HasVariations()) {
    return;
  }

  uint32_t count = ff5->GetFontAxisValueCount();
  if (!count) {
    return;
  }

  RefPtr<IDWriteFontResource> res;
  if (FAILED(ff5->GetFontResource(getter_AddRefs(res)))) {
    return;
  }

  std::vector<DWRITE_FONT_AXIS_VALUE> values(count);
  if (FAILED(ff5->GetFontAxisValues(values.data(), count))) {
    return;
  }

  aOutput->reserve(count);
  for (uint32_t i = 0; i < count; i++) {
    DWRITE_FONT_AXIS_ATTRIBUTES attr = res->GetFontAxisAttributes(i);
    if (attr & DWRITE_FONT_AXIS_ATTRIBUTES_VARIABLE) {
      float v = values[i].value;
      uint32_t t = TRUETYPE_TAG(
          uint8_t(values[i].axisTag), uint8_t(values[i].axisTag >> 8),
          uint8_t(values[i].axisTag >> 16), uint8_t(values[i].axisTag >> 24));
      aOutput->push_back(wr::FontVariation{uint32_t(t), float(v)});
    }
  }
}

bool ScaledFontDWrite::GetFontInstanceData(FontInstanceDataOutput aCb,
                                           void* aBaton) {
  InstanceData instance(this);

  // If the font has variations, get the list of axis values.
  std::vector<wr::FontVariation> variations;
  GetVariationsFromFontFace(mFontFace, &variations);

  return aCb(reinterpret_cast<uint8_t*>(&instance), sizeof(instance),
             variations.data(), variations.size(), aBaton);
}

bool ScaledFontDWrite::GetWRFontInstanceOptions(
    Maybe<wr::FontInstanceOptions>* aOutOptions,
    Maybe<wr::FontInstancePlatformOptions>* aOutPlatformOptions,
    std::vector<wr::FontVariation>* aOutVariations) {
  wr::FontInstanceOptions options = {};
  options.render_mode = wr::ToFontRenderMode(GetDefaultAAMode());
  options.flags = wr::FontInstanceFlags{0};
  if (HasBoldSimulation()) {
    options.flags |= wr::FontInstanceFlags::SYNTHETIC_BOLD;
  }
  if (UseMultistrikeBold()) {
    options.flags |= wr::FontInstanceFlags::MULTISTRIKE_BOLD;
  }
  if (UseEmbeddedBitmaps()) {
    options.flags |= wr::FontInstanceFlags::EMBEDDED_BITMAPS;
  }
  if (ForceGDIMode()) {
    options.flags |= wr::FontInstanceFlags::FORCE_GDI;
  } else {
    options.flags |= wr::FontInstanceFlags::SUBPIXEL_POSITION;
  }
  auto& settings = DWriteSettings();
  switch (settings.RenderingMode()) {
    case DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC:
      options.flags |= wr::FontInstanceFlags::FORCE_SYMMETRIC;
      break;
    case DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL:
      options.flags |= wr::FontInstanceFlags::NO_SYMMETRIC;
      break;
    default:
      break;
  }
  if (Factory::GetSubpixelOrder() == SubpixelOrder::BGR) {
    options.flags |= wr::FontInstanceFlags::SUBPIXEL_BGR;
  }
  options.synthetic_italics =
      wr::DegreesToSyntheticItalics(GetSyntheticObliqueAngle());

  wr::FontInstancePlatformOptions platformOptions;
  platformOptions.gamma = uint16_t(std::round(settings.Gamma() * 100.0f));
  platformOptions.contrast =
      uint8_t(std::round(std::min(settings.EnhancedContrast(), 1.0f) * 100.0f));
  platformOptions.cleartype_level =
      uint8_t(std::round(std::min(settings.ClearTypeLevel(), 1.0f) * 100.0f));

  *aOutOptions = Some(options);
  *aOutPlatformOptions = Some(platformOptions);

  GetVariationsFromFontFace(mFontFace, aOutVariations);

  return true;
}

DWriteSettings& ScaledFontDWrite::DWriteSettings() const {
  return DWriteSettings::Get(mGDIForced);
}

// Helper for UnscaledFontDWrite::CreateScaledFont: create a clone of the
// given IDWriteFontFace, with specified variation-axis values applied.
// Returns nullptr in case of failure.
static already_AddRefed<IDWriteFontFace5> CreateFaceWithVariations(
    IDWriteFontFace* aFace, DWRITE_FONT_SIMULATIONS aSimulations,
    const wr::FontVariation* aVariations = nullptr,
    uint32_t aNumVariations = 0) {
  auto makeDWriteAxisTag = [](uint32_t aTag) {
    return DWRITE_MAKE_FONT_AXIS_TAG((aTag >> 24) & 0xff, (aTag >> 16) & 0xff,
                                     (aTag >> 8) & 0xff, aTag & 0xff);
  };

  MOZ_SEH_TRY {
    RefPtr<IDWriteFontFace5> ff5;
    aFace->QueryInterface(__uuidof(IDWriteFontFace5),
                          (void**)getter_AddRefs(ff5));
    if (!ff5) {
      return nullptr;
    }

    RefPtr<IDWriteFontResource> res;
    if (FAILED(ff5->GetFontResource(getter_AddRefs(res)))) {
      return nullptr;
    }

    std::vector<DWRITE_FONT_AXIS_VALUE> fontAxisValues;
    if (aNumVariations) {
      fontAxisValues.reserve(aNumVariations);
      for (uint32_t i = 0; i < aNumVariations; i++) {
        DWRITE_FONT_AXIS_VALUE axisValue = {
            makeDWriteAxisTag(aVariations[i].tag), aVariations[i].value};
        fontAxisValues.push_back(axisValue);
      }
    } else {
      uint32_t count = ff5->GetFontAxisValueCount();
      if (count) {
        fontAxisValues.resize(count);
        if (FAILED(ff5->GetFontAxisValues(fontAxisValues.data(), count))) {
          fontAxisValues.clear();
        }
      }
    }

    RefPtr<IDWriteFontFace5> newFace;
    if (FAILED(res->CreateFontFace(aSimulations, fontAxisValues.data(),
                                   fontAxisValues.size(),
                                   getter_AddRefs(newFace)))) {
      return nullptr;
    }
    return newFace.forget();
  }
  MOZ_SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
    gfxCriticalNote << "Exception occurred initializing variation face";
    return nullptr;
  }
}

bool UnscaledFontDWrite::InitBold() {
  if (mFontFaceBold) {
    return true;
  }

  DWRITE_FONT_SIMULATIONS sims = mFontFace->GetSimulations();
  if (sims & DWRITE_FONT_SIMULATIONS_BOLD) {
    mFontFaceBold = mFontFace;
    return true;
  }
  sims |= DWRITE_FONT_SIMULATIONS_BOLD;

  RefPtr<IDWriteFontFace5> ff5 = CreateFaceWithVariations(mFontFace, sims);
  if (ff5) {
    mFontFaceBold = ff5;
  } else {
    MOZ_SEH_TRY {
      UINT32 numFiles = 0;
      if (FAILED(mFontFace->GetFiles(&numFiles, nullptr))) {
        return false;
      }
      StackArray<IDWriteFontFile*, 1> files(numFiles);
      if (FAILED(mFontFace->GetFiles(&numFiles, files.data()))) {
        return false;
      }
      HRESULT hr = Factory::GetDWriteFactory()->CreateFontFace(
          mFontFace->GetType(), numFiles, files.data(), mFontFace->GetIndex(),
          sims, getter_AddRefs(mFontFaceBold));
      for (UINT32 i = 0; i < numFiles; ++i) {
        files[i]->Release();
      }
      if (FAILED(hr) || !mFontFaceBold) {
        return false;
      }
    }
    MOZ_SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
      gfxCriticalNote << "Exception occurred initializing bold face";
      return false;
    }
  }
  return true;
}

already_AddRefed<ScaledFont> UnscaledFontDWrite::CreateScaledFont(
    Float aGlyphSize, const uint8_t* aInstanceData,
    uint32_t aInstanceDataLength, const wr::FontVariation* aVariations,
    uint32_t aNumVariations) {
  if (aInstanceDataLength < sizeof(ScaledFontDWrite::InstanceData)) {
    gfxWarning() << "DWrite scaled font instance data is truncated.";
    return nullptr;
  }
  const ScaledFontDWrite::InstanceData& instanceData =
      *reinterpret_cast<const ScaledFontDWrite::InstanceData*>(aInstanceData);

  IDWriteFontFace* face = mFontFace;
  if (instanceData.mUseBoldSimulation) {
    if (!InitBold()) {
      gfxWarning() << "Failed creating bold IDWriteFontFace.";
      return nullptr;
    }
    face = mFontFaceBold;
  }
  DWRITE_FONT_SIMULATIONS sims = face->GetSimulations();

  // If variations are required, we create a separate IDWriteFontFace5 with
  // the requested settings applied.
  RefPtr<IDWriteFontFace5> ff5;
  if (aNumVariations) {
    ff5 =
        CreateFaceWithVariations(mFontFace, sims, aVariations, aNumVariations);
    if (ff5) {
      face = ff5;
    } else {
      gfxWarning() << "Failed to create IDWriteFontFace5 with variations.";
    }
  }

  return MakeAndAddRef<ScaledFontDWrite>(
      face, this, aGlyphSize, instanceData.mUseEmbeddedBitmap,
      instanceData.mUseMultistrikeBold, instanceData.mGDIForced, nullptr);
}

already_AddRefed<ScaledFont> UnscaledFontDWrite::CreateScaledFontFromWRFont(
    Float aGlyphSize, const wr::FontInstanceOptions* aOptions,
    const wr::FontInstancePlatformOptions* aPlatformOptions,
    const wr::FontVariation* aVariations, uint32_t aNumVariations) {
  ScaledFontDWrite::InstanceData instanceData(aOptions, aPlatformOptions);
  return CreateScaledFont(aGlyphSize, reinterpret_cast<uint8_t*>(&instanceData),
                          sizeof(instanceData), aVariations, aNumVariations);
}

AntialiasMode ScaledFontDWrite::GetDefaultAAMode() {
  AntialiasMode defaultMode = GetSystemDefaultAAMode();

  switch (defaultMode) {
    case AntialiasMode::SUBPIXEL:
    case AntialiasMode::DEFAULT:
      if (DWriteSettings().ClearTypeLevel() == 0.0f) {
        defaultMode = AntialiasMode::GRAY;
      }
      break;
    case AntialiasMode::GRAY:
      if (!DoGrayscale(mFontFace, mSize)) {
        defaultMode = AntialiasMode::NONE;
      }
      break;
    case AntialiasMode::NONE:
      break;
  }

  return defaultMode;
}

cairo_font_face_t* ScaledFontDWrite::CreateCairoFontFace(
    cairo_font_options_t* aFontOptions) {
  if (!mFontFace) {
    return nullptr;
  }

  return cairo_dwrite_font_face_create_for_dwrite_fontface(mFontFace);
}

void ScaledFontDWrite::PrepareCairoScaledFont(cairo_scaled_font_t* aFont) {
#if 0
  if (mGDIForced) {
    cairo_dwrite_scaled_font_set_force_GDI_classic(aFont, true);
  }
#endif
}

already_AddRefed<UnscaledFont> UnscaledFontDWrite::CreateFromFontDescriptor(
    const uint8_t* aData, uint32_t aDataLength, uint32_t aIndex) {
  // Note that despite the type of aData here, it actually points to a 16-bit
  // Windows font file path (hence the cast to WCHAR* below) and must be null-
  // terminated.
  const WCHAR* path = (const WCHAR*)aData;
  size_t pathLen = aDataLength / sizeof(WCHAR);
  if (pathLen < 1 || path[pathLen - 1] != 0) {
    gfxWarning() << "DWrite font descriptor is truncated.";
    return nullptr;
  }

  RefPtr<IDWriteFactory> factory = Factory::GetDWriteFactory();
  if (!factory) {
    return nullptr;
  }

  MOZ_SEH_TRY {
    RefPtr<IDWriteFontFile> fontFile;
    HRESULT hr = factory->CreateFontFileReference(path, nullptr,
                                                  getter_AddRefs(fontFile));
    if (FAILED(hr)) {
      return nullptr;
    }
    BOOL isSupported;
    DWRITE_FONT_FILE_TYPE fileType;
    DWRITE_FONT_FACE_TYPE faceType;
    UINT32 numFaces;
    hr = fontFile->Analyze(&isSupported, &fileType, &faceType, &numFaces);
    if (FAILED(hr) || !isSupported || aIndex >= numFaces) {
      return nullptr;
    }
    IDWriteFontFile* fontFiles[1] = {fontFile.get()};
    RefPtr<IDWriteFontFace> fontFace;
    hr = factory->CreateFontFace(faceType, 1, fontFiles, aIndex,
                                 DWRITE_FONT_SIMULATIONS_NONE,
                                 getter_AddRefs(fontFace));
    if (FAILED(hr)) {
      return nullptr;
    }
    RefPtr unscaledFont = new UnscaledFontDWrite(fontFace, nullptr);
    unscaledFont->mFontFileName.assign(path, path + pathLen);
    return unscaledFont.forget();
  }
  MOZ_SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
    gfxCriticalNote
        << "Exception occurred creating UnscaledFont for "
        << NS_ConvertUTF16toUTF8((const char16_t*)path, pathLen - 1).get();
    return nullptr;
  }
}

}  // namespace gfx
}  // namespace mozilla
