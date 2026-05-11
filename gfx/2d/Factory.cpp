/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "2D.h"
#include "Swizzle.h"

#ifdef USE_CAIRO
#  include "DrawTargetCairo.h"
#  include "PathCairo.h"
#  include "SourceSurfaceCairo.h"
#endif

#include "DrawTargetSkia.h"
#include "PathSkia.h"
#include "ScaledFontBase.h"

#if defined(WIN32)
#  include "ScaledFontWin.h"
#  include "NativeFontResourceGDI.h"
#  include "UnscaledFontGDI.h"
#endif

#ifdef XP_DARWIN
#  include "ScaledFontMac.h"
#  include "NativeFontResourceMac.h"
#  include "UnscaledFontMac.h"
#endif

#ifdef MOZ_WIDGET_GTK
#  include "ScaledFontFontconfig.h"
#  include "NativeFontResourceFreeType.h"
#  include "UnscaledFontFreeType.h"
#endif

#ifdef MOZ_WIDGET_ANDROID
#  include "ScaledFontFreeType.h"
#  include "NativeFontResourceFreeType.h"
#  include "UnscaledFontFreeType.h"
#endif

#ifdef WIN32
#  include "ScaledFontDWrite.h"
#  include "NativeFontResourceDWrite.h"
#  include "UnscaledFontDWrite.h"
#  include <d3d10_1.h>
#  include <stdlib.h>
#  include "HelpersWin.h"
#  include "ImageContainer.h"
#  include "mozilla/layers/LayersSurfaces.h"
#  include "mozilla/layers/TextureD3D11.h"
#  include "mozilla/layers/VideoProcessorD3D11.h"
#  include "nsWindowsHelpers.h"
#endif

#include "DrawTargetOffset.h"
#include "DrawTargetRecording.h"
#include "PathRecording.h"

#include "SourceSurfaceRawData.h"

#include "mozilla/CheckedInt.h"

#ifdef MOZ_ENABLE_FREETYPE
#  include "ft2build.h"
#  include FT_FREETYPE_H
#endif
#include "mozilla/StaticPrefs_gfx.h"

#if defined(MOZ_LOGGING)
GFX2D_API mozilla::LogModule* GetGFX2DLog() {
  static mozilla::LazyLogModule sLog("gfx2d");
  return sLog;
}
#endif

#ifdef MOZ_ENABLE_FREETYPE
extern "C" {

void mozilla_AddRefSharedFTFace(void* aContext) {
  if (aContext) {
    static_cast<mozilla::gfx::SharedFTFace*>(aContext)->AddRef();
  }
}

void mozilla_ReleaseSharedFTFace(void* aContext, void* aOwner) {
  if (aContext) {
    auto* sharedFace = static_cast<mozilla::gfx::SharedFTFace*>(aContext);
    sharedFace->ForgetLockOwner(aOwner);
    sharedFace->Release();
  }
}

void mozilla_ForgetSharedFTFaceLockOwner(void* aContext, void* aOwner) {
  static_cast<mozilla::gfx::SharedFTFace*>(aContext)->ForgetLockOwner(aOwner);
}

int mozilla_LockSharedFTFace(void* aContext,
                             void* aOwner) MOZ_NO_THREAD_SAFETY_ANALYSIS {
  return int(static_cast<mozilla::gfx::SharedFTFace*>(aContext)->Lock(aOwner));
}

void mozilla_UnlockSharedFTFace(void* aContext) MOZ_NO_THREAD_SAFETY_ANALYSIS {
  static_cast<mozilla::gfx::SharedFTFace*>(aContext)->Unlock();
}

FT_Error mozilla_LoadFTGlyph(FT_Face aFace, uint32_t aGlyphIndex,
                             int32_t aFlags) {
  return mozilla::gfx::Factory::LoadFTGlyph(aFace, aGlyphIndex, aFlags);
}

void mozilla_LockFTLibrary(FT_Library aFTLibrary) {
  mozilla::gfx::Factory::LockFTLibrary(aFTLibrary);
}

void mozilla_UnlockFTLibrary(FT_Library aFTLibrary) {
  mozilla::gfx::Factory::UnlockFTLibrary(aFTLibrary);
}
}
#endif

namespace mozilla::gfx {

#ifdef MOZ_ENABLE_FREETYPE
FT_Library Factory::mFTLibrary = nullptr;
StaticMutex Factory::mFTLock;

already_AddRefed<SharedFTFace> FTUserFontData::CloneFace(int aFaceIndex) {
  if (mFontData) {
    RefPtr<SharedFTFace> face = Factory::NewSharedFTFaceFromData(
        nullptr, mFontData, mLength, aFaceIndex, this);
    if (!face ||
        (FT_Select_Charmap(face->GetFace(), FT_ENCODING_UNICODE) != FT_Err_Ok &&
         FT_Select_Charmap(face->GetFace(), FT_ENCODING_MS_SYMBOL) !=
             FT_Err_Ok)) {
      return nullptr;
    }
    return face.forget();
  }
  FT_Face face = Factory::NewFTFace(nullptr, mFilename.c_str(), aFaceIndex);
  if (face) {
    return MakeAndAddRef<SharedFTFace>(face, this);
  }
  return nullptr;
}
#endif

#ifdef WIN32
// Note: mDeviceLock must be held when mutating these values.
StaticRefPtr<ID3D11Device> Factory::mD3D11Device;
StaticRefPtr<IDWriteFactory> Factory::mDWriteFactory;
StaticRefPtr<IDWriteFontCollection> Factory::mDWriteSystemFonts;
StaticMutex Factory::mDeviceLock;
#endif

SubpixelOrder Factory::mSubpixelOrder = SubpixelOrder::UNKNOWN;

mozilla::gfx::Config* Factory::sConfig = nullptr;

void Factory::Init(const Config& aConfig) {
  MOZ_ASSERT(!sConfig);
  sConfig = new Config(aConfig);

#ifdef XP_DARWIN
  NativeFontResourceMac::RegisterMemoryReporter();
#else
  NativeFontResource::RegisterMemoryReporter();
#endif

  SourceSurfaceAlignedRawData::RegisterMemoryReporter();
}

void Factory::ShutDown() {
  if (sConfig) {
    delete sConfig->mLogForwarder;
    delete sConfig;
    sConfig = nullptr;
  }

#ifdef MOZ_ENABLE_FREETYPE
  mFTLibrary = nullptr;
#endif
}

// If the size is "reasonable", we want gfxCriticalError to assert, so
// this is the option set up for it.
inline int LoggerOptionsBasedOnSize(const IntSize& aSize) {
  return CriticalLog::DefaultOptions(Factory::ReasonableSurfaceSize(aSize));
}

bool Factory::ReasonableSurfaceSize(const IntSize& aSize) {
  return Factory::CheckSurfaceSize(aSize, kReasonableSurfaceSize);
}

bool Factory::AllowedSurfaceSize(const IntSize& aSize) {
  if (sConfig) {
    return Factory::CheckSurfaceSize(aSize, sConfig->mMaxTextureSize,
                                     sConfig->mMaxAllocSize);
  }

  return CheckSurfaceSize(aSize);
}

bool Factory::CheckSurfaceSize(const IntSize& sz, int32_t extentLimit,
                               int32_t allocLimit) {
  if (sz.width <= 0 || sz.height <= 0) {
    return false;
  }

  // reject images with sides bigger than limit
  if (extentLimit && (sz.width > extentLimit || sz.height > extentLimit)) {
    gfxDebug() << "Surface size too large (exceeds extent limit)!";
    return false;
  }

  // assuming 4 bytes per pixel, make sure the allocation size
  // doesn't overflow a int32_t either
  auto stride = GetAlignedStride<16>(sz.width, 4);
  if (stride.isNothing()) {
    gfxDebug() << "Surface size too large (stride is invalid)!";
    return false;
  }

  CheckedInt<int32_t> numBytes =
      CheckedInt<int32_t>(stride.value()) * sz.height;
  if (!numBytes.isValid()) {
    gfxDebug()
        << "Surface size too large (allocation size would overflow int32_t)!";
    return false;
  }

  if (allocLimit && allocLimit < numBytes.value()) {
    gfxDebug() << "Surface size too large (exceeds allocation limit)!";
    return false;
  }

  return true;
}

already_AddRefed<DrawTarget> Factory::CreateDrawTarget(BackendType aBackend,
                                                       const IntSize& aSize,
                                                       SurfaceFormat aFormat) {
  if (!AllowedSurfaceSize(aSize)) {
    gfxCriticalError(LoggerOptionsBasedOnSize(aSize))
        << "Failed to allocate a surface due to invalid size (CDT) " << aSize;
    return nullptr;
  }

  RefPtr<DrawTarget> retVal;
  switch (aBackend) {
    case BackendType::SKIA: {
      RefPtr<DrawTargetSkia> newTarget;
      newTarget = new DrawTargetSkia();
      if (newTarget->Init(aSize, aFormat)) {
        retVal = newTarget;
      }
      break;
    }
#ifdef USE_CAIRO
    case BackendType::CAIRO: {
      RefPtr<DrawTargetCairo> newTarget;
      newTarget = new DrawTargetCairo();
      if (newTarget->Init(aSize, aFormat)) {
        retVal = newTarget;
      }
      break;
    }
#endif
    default:
      return nullptr;
  }

  if (!retVal) {
    // Failed
    gfxCriticalError(LoggerOptionsBasedOnSize(aSize))
        << "Failed to create DrawTarget, Type: " << int(aBackend)
        << " Size: " << aSize;
  }

  return retVal.forget();
}

already_AddRefed<PathBuilder> Factory::CreatePathBuilder(BackendType aBackend,
                                                         FillRule aFillRule) {
  switch (aBackend) {
    case BackendType::SKIA:
    case BackendType::WEBGL:
      return PathBuilderSkia::Create(aFillRule);
#ifdef USE_CAIRO
    case BackendType::CAIRO:
      return PathBuilderCairo::Create(aFillRule);
#endif
    case BackendType::RECORDING:
      return do_AddRef(new PathBuilderRecording(BackendType::SKIA, aFillRule));
    default:
      gfxCriticalNote << "Invalid PathBuilder type specified: "
                      << (int)aBackend;
      return nullptr;
  }
}

already_AddRefed<PathBuilder> Factory::CreateSimplePathBuilder() {
  return CreatePathBuilder(BackendType::SKIA);
}

already_AddRefed<DrawTarget> Factory::CreateRecordingDrawTarget(
    DrawEventRecorder* aRecorder, DrawTarget* aDT, IntRect aRect) {
  return MakeAndAddRef<DrawTargetRecording>(aRecorder, aDT, aRect);
}

already_AddRefed<DrawTarget> Factory::CreateDrawTargetForData(
    BackendType aBackend, unsigned char* aData, const IntSize& aSize,
    int32_t aStride, SurfaceFormat aFormat, bool aUninitialized,
    bool aIsClear) {
  MOZ_ASSERT(aData);
  if (!AllowedSurfaceSize(aSize)) {
    gfxCriticalError(LoggerOptionsBasedOnSize(aSize))
        << "Failed to allocate a surface due to invalid size (DTD) " << aSize;
    return nullptr;
  }

  RefPtr<DrawTarget> retVal;

  switch (aBackend) {
    case BackendType::SKIA: {
      RefPtr<DrawTargetSkia> newTarget;
      newTarget = new DrawTargetSkia();
      if (newTarget->Init(aData, aSize, aStride, aFormat, aUninitialized,
                          aIsClear)) {
        retVal = newTarget;
      }
      break;
    }
#ifdef USE_CAIRO
    case BackendType::CAIRO: {
      RefPtr<DrawTargetCairo> newTarget;
      newTarget = new DrawTargetCairo();
      if (newTarget->Init(aData, aSize, aStride, aFormat)) {
        retVal = std::move(newTarget);
      }
      break;
    }
#endif
    default:
      gfxCriticalNote << "Invalid draw target type specified: "
                      << (int)aBackend;
      return nullptr;
  }

  if (!retVal) {
    gfxCriticalNote << "Failed to create DrawTarget, Type: " << int(aBackend)
                    << " Size: " << aSize << ", Data: " << hexa((void*)aData)
                    << ", Stride: " << aStride;
  }

  return retVal.forget();
}

already_AddRefed<DrawTarget> Factory::CreateOffsetDrawTarget(
    DrawTarget* aDrawTarget, IntPoint aTileOrigin) {
  RefPtr<DrawTargetOffset> dt = new DrawTargetOffset();

  if (!dt->Init(aDrawTarget, aTileOrigin)) {
    return nullptr;
  }

  return dt.forget();
}

bool Factory::DoesBackendSupportDataDrawtarget(BackendType aType) {
  switch (aType) {
    case BackendType::RECORDING:
    case BackendType::NONE:
    case BackendType::BACKEND_LAST:
    case BackendType::WEBRENDER_TEXT:
    case BackendType::WEBGL:
      return false;
    case BackendType::CAIRO:
    case BackendType::SKIA:
      return true;
  }

  return false;
}

size_t Factory::GetMaxSurfaceSize(BackendType aType) {
  switch (aType) {
    case BackendType::CAIRO:
      return DrawTargetCairo::GetMaxSurfaceSize();
    case BackendType::SKIA:
      return DrawTargetSkia::GetMaxSurfaceSize();
    default:
      return 0;
  }
}

size_t Factory::GetMaxSurfaceArea(BackendType aType) {
  switch (aType) {
    case BackendType::CAIRO:
      return DrawTargetCairo::GetMaxSurfaceArea();
    case BackendType::SKIA:
      return DrawTargetSkia::GetMaxSurfaceArea();
    default:
      return 0;
  }
}

already_AddRefed<NativeFontResource> Factory::CreateNativeFontResource(
    const uint8_t* aData, uint32_t aSize, FontType aFontType,
    void* aFontContext) {
  switch (aFontType) {
#ifdef WIN32
    case FontType::DWRITE:
      return NativeFontResourceDWrite::Create(aData, aSize);
    case FontType::GDI:
      return NativeFontResourceGDI::Create(aData, aSize);
#elif defined(XP_DARWIN)
    case FontType::MAC:
      return NativeFontResourceMac::Create(aData, aSize);
#elif defined(MOZ_WIDGET_GTK)
    case FontType::FONTCONFIG:
      return NativeFontResourceFontconfig::Create(
          aData, aSize, static_cast<FT_Library>(aFontContext));
#elif defined(MOZ_WIDGET_ANDROID)
    case FontType::FREETYPE:
      return NativeFontResourceFreeType::Create(
          aData, aSize, static_cast<FT_Library>(aFontContext));
#endif
    default:
      gfxWarning()
          << "Unable to create requested font resource from truetype data";
      return nullptr;
  }
}

already_AddRefed<UnscaledFont> Factory::CreateUnscaledFontFromFontDescriptor(
    FontType aType, const uint8_t* aData, uint32_t aDataLength,
    uint32_t aIndex) {
  switch (aType) {
#ifdef WIN32
    case FontType::DWRITE:
      return UnscaledFontDWrite::CreateFromFontDescriptor(aData, aDataLength,
                                                          aIndex);
    case FontType::GDI:
      return UnscaledFontGDI::CreateFromFontDescriptor(aData, aDataLength,
                                                       aIndex);
#elif defined(XP_DARWIN)
    case FontType::MAC:
      return UnscaledFontMac::CreateFromFontDescriptor(aData, aDataLength,
                                                       aIndex);
#elif defined(MOZ_WIDGET_GTK)
    case FontType::FONTCONFIG:
      return UnscaledFontFontconfig::CreateFromFontDescriptor(
          aData, aDataLength, aIndex);
#elif defined(MOZ_WIDGET_ANDROID)
    case FontType::FREETYPE:
      return UnscaledFontFreeType::CreateFromFontDescriptor(aData, aDataLength,
                                                            aIndex);
#endif
    default:
      gfxWarning() << "Invalid type specified for UnscaledFont font descriptor";
      return nullptr;
  }
}

#ifdef XP_DARWIN
already_AddRefed<ScaledFont> Factory::CreateScaledFontForMacFont(
    CGFontRef aCGFont, const RefPtr<UnscaledFont>& aUnscaledFont, Float aSize,
    bool aUseFontSmoothing, bool aApplySyntheticBold, bool aHasColorGlyphs) {
  return MakeAndAddRef<ScaledFontMac>(aCGFont, aUnscaledFont, aSize, false,
                                      aUseFontSmoothing, aApplySyntheticBold,
                                      aHasColorGlyphs);
}
#endif

#ifdef MOZ_WIDGET_GTK
already_AddRefed<ScaledFont> Factory::CreateScaledFontForFontconfigFont(
    const RefPtr<UnscaledFont>& aUnscaledFont, Float aSize,
    RefPtr<SharedFTFace> aFace, FcPattern* aPattern) {
  return MakeAndAddRef<ScaledFontFontconfig>(std::move(aFace), aPattern,
                                             aUnscaledFont, aSize);
}
#endif

#ifdef MOZ_WIDGET_ANDROID
already_AddRefed<ScaledFont> Factory::CreateScaledFontForFreeTypeFont(
    const RefPtr<UnscaledFont>& aUnscaledFont, Float aSize,
    RefPtr<SharedFTFace> aFace, bool aApplySyntheticBold) {
  return MakeAndAddRef<ScaledFontFreeType>(std::move(aFace), aUnscaledFont,
                                           aSize, aApplySyntheticBold);
}
#endif

void Factory::SetSubpixelOrder(SubpixelOrder aOrder) {
  mSubpixelOrder = aOrder;
}

SubpixelOrder Factory::GetSubpixelOrder() { return mSubpixelOrder; }

#ifdef MOZ_ENABLE_FREETYPE
SharedFTFace::SharedFTFace(FT_Face aFace, SharedFTFaceData* aData)
    : mFace(aFace),
      mData(aData),
      mLock("SharedFTFace::mLock"),
      mLastLockOwner(nullptr) {
  if (mData) {
    mData->BindData();
  }
}

SharedFTFace::~SharedFTFace() {
  Factory::ReleaseFTFace(mFace);
  if (mData) {
    mData->ReleaseData();
  }
}

void Factory::SetFTLibrary(FT_Library aFTLibrary) { mFTLibrary = aFTLibrary; }

FT_Library Factory::GetFTLibrary() {
  MOZ_ASSERT(mFTLibrary);
  return mFTLibrary;
}

FT_Library Factory::NewFTLibrary() {
  FT_Library library;
  if (FT_Init_FreeType(&library) != FT_Err_Ok) {
    return nullptr;
  }
  return library;
}

void Factory::ReleaseFTLibrary(FT_Library aFTLibrary) {
  FT_Done_FreeType(aFTLibrary);
}

void Factory::LockFTLibrary(FT_Library aFTLibrary)
    MOZ_CAPABILITY_ACQUIRE(mFTLock) MOZ_NO_THREAD_SAFETY_ANALYSIS {
  mFTLock.Lock();
}

void Factory::UnlockFTLibrary(FT_Library aFTLibrary)
    MOZ_CAPABILITY_RELEASE(mFTLock) MOZ_NO_THREAD_SAFETY_ANALYSIS {
  mFTLock.Unlock();
}

FT_Face Factory::NewFTFace(FT_Library aFTLibrary, const char* aFileName,
                           int aFaceIndex) {
  StaticMutexAutoLock lock(mFTLock);
  if (!aFTLibrary) {
    aFTLibrary = mFTLibrary;
  }
  FT_Face face;
  if (FT_New_Face(aFTLibrary, aFileName, aFaceIndex, &face) != FT_Err_Ok) {
    return nullptr;
  }
  return face;
}

already_AddRefed<SharedFTFace> Factory::NewSharedFTFace(FT_Library aFTLibrary,
                                                        const char* aFilename,
                                                        int aFaceIndex) {
  FT_Face face = NewFTFace(aFTLibrary, aFilename, aFaceIndex);
  if (!face) {
    return nullptr;
  }

  RefPtr<FTUserFontData> data;
#  ifdef ANDROID
  // If the font has variations, we may later need to "clone" it in
  // UnscaledFontFreeType::CreateScaledFont. To support this, we attach an
  // FTUserFontData that records the filename used to instantiate the face.
  if (face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS) {
    data = new FTUserFontData(aFilename);
  }
#  endif
  return MakeAndAddRef<SharedFTFace>(face, data);
}

FT_Face Factory::NewFTFaceFromData(FT_Library aFTLibrary, const uint8_t* aData,
                                   size_t aDataSize, int aFaceIndex) {
  StaticMutexAutoLock lock(mFTLock);
  if (!aFTLibrary) {
    aFTLibrary = mFTLibrary;
  }
  FT_Face face;
  if (FT_New_Memory_Face(aFTLibrary, aData, aDataSize, aFaceIndex, &face) !=
      FT_Err_Ok) {
    return nullptr;
  }
  return face;
}

already_AddRefed<SharedFTFace> Factory::NewSharedFTFaceFromData(
    FT_Library aFTLibrary, const uint8_t* aData, size_t aDataSize,
    int aFaceIndex, SharedFTFaceData* aSharedData) {
  if (FT_Face face =
          NewFTFaceFromData(aFTLibrary, aData, aDataSize, aFaceIndex)) {
    return MakeAndAddRef<SharedFTFace>(face, aSharedData);
  } else {
    return nullptr;
  }
}

void Factory::ReleaseFTFace(FT_Face aFace) {
  StaticMutexAutoLock lock(mFTLock);
  FT_Done_Face(aFace);
}

FT_Error Factory::LoadFTGlyph(FT_Face aFace, uint32_t aGlyphIndex,
                              int32_t aFlags) {
  StaticMutexAutoLock lock(mFTLock);
  return FT_Load_Glyph(aFace, aGlyphIndex, aFlags);
}
#endif

#ifdef WIN32
bool Factory::SetDirect3D11Device(ID3D11Device* aDevice) {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  StaticMutexAutoLock lock(mDeviceLock);

  mD3D11Device = aDevice;
  return true;
}

RefPtr<ID3D11Device> Factory::GetDirect3D11Device() {
  StaticMutexAutoLock lock(mDeviceLock);
  return mD3D11Device;
}

RefPtr<IDWriteFactory> Factory::GetDWriteFactory() {
  StaticMutexAutoLock lock(mDeviceLock);
  return mDWriteFactory;
}

RefPtr<IDWriteFactory> Factory::EnsureDWriteFactory() {
  StaticMutexAutoLock lock(mDeviceLock);

  if (mDWriteFactory) {
    return mDWriteFactory;
  }

  HMODULE dwriteModule = LoadLibrarySystem32(L"dwrite.dll");
  decltype(DWriteCreateFactory)* createDWriteFactory =
      (decltype(DWriteCreateFactory)*)GetProcAddress(dwriteModule,
                                                     "DWriteCreateFactory");

  if (!createDWriteFactory) {
    gfxWarning() << "Failed to locate DWriteCreateFactory function.";
    return nullptr;
  }

  HRESULT hr =
      createDWriteFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                          reinterpret_cast<IUnknown**>(&mDWriteFactory));

  if (FAILED(hr)) {
    gfxWarning() << "Failed to create DWrite Factory.";
  }

  return mDWriteFactory;
}

RefPtr<IDWriteFontCollection> Factory::GetDWriteSystemFonts(bool aUpdate) {
  StaticMutexAutoLock lock(mDeviceLock);

  if (mDWriteSystemFonts && !aUpdate) {
    return mDWriteSystemFonts;
  }

  if (!mDWriteFactory) {
    if ((rand() & 0x3f) == 0) {
      gfxCriticalError(int(gfx::LogOptions::AssertOnCall))
          << "Failed to create DWrite factory";
    } else {
      gfxWarning() << "Failed to create DWrite factory";
    }

    return nullptr;
  }

  RefPtr<IDWriteFontCollection> systemFonts;
  HRESULT hr =
      mDWriteFactory->GetSystemFontCollection(getter_AddRefs(systemFonts));
  if (FAILED(hr) || !systemFonts) {
    // only crash some of the time so those experiencing this problem
    // don't stop using Firefox
    if ((rand() & 0x3f) == 0) {
      gfxCriticalError(int(gfx::LogOptions::AssertOnCall))
          << "Failed to create DWrite system font collection";
    } else {
      gfxWarning() << "Failed to create DWrite system font collection";
    }
    return nullptr;
  }
  mDWriteSystemFonts = systemFonts;

  return mDWriteSystemFonts;
}

BYTE sSystemTextQuality = CLEARTYPE_QUALITY;
void Factory::SetSystemTextQuality(uint8_t aQuality) {
  sSystemTextQuality = aQuality;
}

already_AddRefed<ScaledFont> Factory::CreateScaledFontForDWriteFont(
    IDWriteFontFace* aFontFace, const gfxFontStyle* aStyle,
    const RefPtr<UnscaledFont>& aUnscaledFont, float aSize,
    bool aUseEmbeddedBitmap, bool aUseMultistrikeBold, bool aGDIForced) {
  return MakeAndAddRef<ScaledFontDWrite>(
      aFontFace, aUnscaledFont, aSize, aUseEmbeddedBitmap, aUseMultistrikeBold,
      aGDIForced, aStyle);
}

already_AddRefed<ScaledFont> Factory::CreateScaledFontForGDIFont(
    const void* aLogFont, const RefPtr<UnscaledFont>& aUnscaledFont,
    Float aSize) {
  return MakeAndAddRef<ScaledFontWin>(static_cast<const LOGFONT*>(aLogFont),
                                      aUnscaledFont, aSize);
}
#endif  // WIN32

already_AddRefed<DrawTarget> Factory::CreateDrawTargetWithSkCanvas(
    SkCanvas* aCanvas) {
  RefPtr<DrawTargetSkia> newTarget = new DrawTargetSkia();
  if (!newTarget->Init(aCanvas)) {
    return nullptr;
  }
  return newTarget.forget();
}

void Factory::PurgeAllCaches() {}

already_AddRefed<DrawTarget> Factory::CreateDrawTargetForCairoSurface(
    cairo_surface_t* aSurface, const IntSize& aSize, SurfaceFormat* aFormat) {
  if (!AllowedSurfaceSize(aSize)) {
    gfxWarning() << "Allowing surface with invalid size (Cairo) " << aSize;
  }

  RefPtr<DrawTarget> retVal;

#ifdef USE_CAIRO
  RefPtr<DrawTargetCairo> newTarget = new DrawTargetCairo();

  if (newTarget->Init(aSurface, aSize, aFormat)) {
    retVal = newTarget;
  }
#endif
  return retVal.forget();
}

already_AddRefed<SourceSurface> Factory::CreateSourceSurfaceForCairoSurface(
    cairo_surface_t* aSurface, const IntSize& aSize, SurfaceFormat aFormat) {
  if (aSize.width <= 0 || aSize.height <= 0) {
    gfxWarning() << "Can't create a SourceSurface without a valid size";
    return nullptr;
  }

#ifdef USE_CAIRO
  return MakeAndAddRef<SourceSurfaceCairo>(aSurface, aSize, aFormat);
#else
  return nullptr;
#endif
}

already_AddRefed<DataSourceSurface> Factory::CreateWrappingDataSourceSurface(
    uint8_t* aData, int32_t aStride, const IntSize& aSize,
    SurfaceFormat aFormat,
    SourceSurfaceDeallocator aDeallocator /* = nullptr */,
    void* aClosure /* = nullptr */) {
  // Just check for negative/zero size instead of the full AllowedSurfaceSize()
  // - since the data is already allocated we do not need to check for a
  // possible overflow - it already worked.
  if (aSize.width <= 0 || aSize.height <= 0) {
    return nullptr;
  }
  if (!aDeallocator && aClosure) {
    return nullptr;
  }

  MOZ_ASSERT(aData);

  RefPtr<SourceSurfaceRawData> newSurf = new SourceSurfaceRawData();
  newSurf->InitWrappingData(aData, aSize, aStride, aFormat, aDeallocator,
                            aClosure);

  return newSurf.forget();
}

already_AddRefed<DataSourceSurface> Factory::CreateDataSourceSurface(
    const IntSize& aSize, SurfaceFormat aFormat, bool aZero) {
  if (!AllowedSurfaceSize(aSize)) {
    gfxCriticalError(LoggerOptionsBasedOnSize(aSize))
        << "Failed to allocate a surface due to invalid size (DSS) " << aSize;
    return nullptr;
  }

  // Skia doesn't support RGBX, so memset RGBX to 0xFF
  bool clearSurface = aZero || aFormat == SurfaceFormat::B8G8R8X8;
  uint8_t clearValue = aFormat == SurfaceFormat::B8G8R8X8 ? 0xFF : 0;

  RefPtr<SourceSurfaceAlignedRawData> newSurf =
      new SourceSurfaceAlignedRawData();
  if (newSurf->Init(aSize, aFormat, clearSurface, clearValue)) {
    return newSurf.forget();
  }

  gfxWarning() << "CreateDataSourceSurface failed in init";
  return nullptr;
}

already_AddRefed<DataSourceSurface> Factory::CreateDataSourceSurfaceWithStride(
    const IntSize& aSize, SurfaceFormat aFormat, int32_t aStride, bool aZero) {
  if (!AllowedSurfaceSize(aSize) ||
      aStride < aSize.width * BytesPerPixel(aFormat)) {
    gfxCriticalError(LoggerOptionsBasedOnSize(aSize))
        << "CreateDataSourceSurfaceWithStride failed with bad stride "
        << aStride << ", " << aSize << ", " << aFormat;
    return nullptr;
  }

  // Skia doesn't support RGBX, so memset RGBX to 0xFF
  bool clearSurface = aZero || aFormat == SurfaceFormat::B8G8R8X8;
  uint8_t clearValue = aFormat == SurfaceFormat::B8G8R8X8 ? 0xFF : 0;

  RefPtr<SourceSurfaceAlignedRawData> newSurf =
      new SourceSurfaceAlignedRawData();
  if (newSurf->Init(aSize, aFormat, clearSurface, clearValue, aStride)) {
    return newSurf.forget();
  }

  gfxCriticalError(LoggerOptionsBasedOnSize(aSize))
      << "CreateDataSourceSurfaceWithStride failed to initialize " << aSize
      << ", " << aFormat << ", " << aStride << ", " << aZero;
  return nullptr;
}

already_AddRefed<DataSourceSurface> Factory::CopyDataSourceSurface(
    DataSourceSurface* aSource) {
  // Don't worry too much about speed.
  MOZ_ASSERT(aSource->GetFormat() == SurfaceFormat::R8G8B8A8 ||
             aSource->GetFormat() == SurfaceFormat::R8G8B8X8 ||
             aSource->GetFormat() == SurfaceFormat::B8G8R8A8 ||
             aSource->GetFormat() == SurfaceFormat::B8G8R8X8 ||
             aSource->GetFormat() == SurfaceFormat::A8);

  DataSourceSurface::ScopedMap srcMap(aSource, DataSourceSurface::READ);
  if (NS_WARN_IF(!srcMap.IsMapped())) {
    MOZ_ASSERT_UNREACHABLE("CopyDataSourceSurface: Failed to map surface.");
    return nullptr;
  }

  IntSize size = aSource->GetSize();
  SurfaceFormat format = aSource->GetFormat();

  RefPtr<DataSourceSurface> dst = CreateDataSourceSurfaceWithStride(
      size, format, srcMap.GetStride(), /* aZero */ false);
  if (NS_WARN_IF(!dst)) {
    return nullptr;
  }

  DataSourceSurface::ScopedMap dstMap(dst, DataSourceSurface::WRITE);
  if (NS_WARN_IF(!dstMap.IsMapped())) {
    MOZ_ASSERT_UNREACHABLE("CopyDataSourceSurface: Failed to map surface.");
    return nullptr;
  }

  SwizzleData(srcMap.GetData(), srcMap.GetStride(), format, dstMap.GetData(),
              dstMap.GetStride(), format, size);
  return dst.forget();
}

void Factory::CopyDataSourceSurface(DataSourceSurface* aSource,
                                    DataSourceSurface* aDest) {
  // Don't worry too much about speed.
  MOZ_ASSERT(aSource->GetSize() == aDest->GetSize());
  MOZ_ASSERT(aSource->GetFormat() == SurfaceFormat::R8G8B8A8 ||
             aSource->GetFormat() == SurfaceFormat::R8G8B8X8 ||
             aSource->GetFormat() == SurfaceFormat::B8G8R8A8 ||
             aSource->GetFormat() == SurfaceFormat::B8G8R8X8 ||
             aSource->GetFormat() == SurfaceFormat::A8);
  MOZ_ASSERT(aDest->GetFormat() == SurfaceFormat::R8G8B8A8 ||
             aDest->GetFormat() == SurfaceFormat::R8G8B8X8 ||
             aDest->GetFormat() == SurfaceFormat::B8G8R8A8 ||
             aDest->GetFormat() == SurfaceFormat::B8G8R8X8 ||
             aDest->GetFormat() == SurfaceFormat::R5G6B5_UINT16 ||
             aDest->GetFormat() == SurfaceFormat::A8);

  DataSourceSurface::MappedSurface srcMap;
  DataSourceSurface::MappedSurface destMap;
  if (!aSource->Map(DataSourceSurface::MapType::READ, &srcMap) ||
      !aDest->Map(DataSourceSurface::MapType::WRITE, &destMap)) {
    MOZ_ASSERT(false, "CopyDataSourceSurface: Failed to map surface.");
    return;
  }

  SwizzleData(srcMap.mData, srcMap.mStride, aSource->GetFormat(), destMap.mData,
              destMap.mStride, aDest->GetFormat(), aSource->GetSize());

  aSource->Unmap();
  aDest->Unmap();
}

#ifdef WIN32

/* static */
already_AddRefed<DataSourceSurface>
Factory::CreateBGRA8DataSourceSurfaceForD3D11Texture(
    ID3D11Texture2D* aSrcTexture, uint32_t aArrayIndex,
    gfx::ColorSpace2 aColorSpace, gfx::ColorRange aColorRange,
    gfx::TransferFunction aTransferFunction) {
  D3D11_TEXTURE2D_DESC srcDesc = {0};
  aSrcTexture->GetDesc(&srcDesc);

  RefPtr<gfx::DataSourceSurface> destTexture =
      gfx::Factory::CreateDataSourceSurface(
          IntSize(srcDesc.Width, srcDesc.Height), gfx::SurfaceFormat::B8G8R8A8);
  if (NS_WARN_IF(!destTexture)) {
    return nullptr;
  }
  if (!ReadbackTexture(destTexture, aSrcTexture, aArrayIndex, aColorSpace,
                       aColorRange, aTransferFunction)) {
    return nullptr;
  }
  return destTexture.forget();
}

/* static */ nsresult Factory::CreateSdbForD3D11Texture(
    ID3D11Texture2D* aSrcTexture, const IntSize& aSrcSize,
    layers::SurfaceDescriptorBuffer& aSdBuffer,
    const std::function<layers::MemoryOrShmem(uint32_t)>& aAllocate) {
  D3D11_TEXTURE2D_DESC srcDesc = {0};
  aSrcTexture->GetDesc(&srcDesc);
  if (srcDesc.Width != uint32_t(aSrcSize.width) ||
      srcDesc.Height != uint32_t(aSrcSize.height) ||
      srcDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  const auto format = gfx::SurfaceFormat::B8G8R8A8;
  uint8_t* buffer = nullptr;
  int32_t stride = 0;
  nsresult rv = layers::Image::AllocateSurfaceDescriptorBufferRgb(
      aSrcSize, format, buffer, aSdBuffer, stride, aAllocate);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (!ReadbackTexture(buffer, stride, aSrcTexture)) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

/* static */
bool Factory::ConvertSourceAndRetryReadback(
    DataSourceSurface* aDestCpuTexture, ID3D11Texture2D* aSrcTexture,
    uint32_t aArrayIndex, gfx::ColorSpace2 aColorSpace,
    gfx::ColorRange aColorRange, gfx::TransferFunction aTransferFunction) {
  MOZ_ASSERT(aDestCpuTexture);
  MOZ_ASSERT(aSrcTexture);

  RefPtr<ID3D11Device> device;
  aSrcTexture->GetDevice(getter_AddRefs(device));
  if (!device) {
    gfxWarning() << "Failed to get D3D11 device from source texture";
    return false;
  }

  CD3D11_TEXTURE2D_DESC desc;
  aSrcTexture->GetDesc(&desc);

  if (desc.Format != DXGI_FORMAT_NV12 && desc.Format != DXGI_FORMAT_P010 &&
      desc.Format != DXGI_FORMAT_P016) {
    gfxWarning() << "Unexpected DXGI format";
    return false;
  }

  desc = CD3D11_TEXTURE2D_DESC(
      DXGI_FORMAT_B8G8R8A8_UNORM, desc.Width, desc.Height, 1, 1,
      D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);

  RefPtr<ID3D11Texture2D> newSrcTexture;
  HRESULT hr =
      device->CreateTexture2D(&desc, nullptr, getter_AddRefs(newSrcTexture));
  if (FAILED(hr)) {
    gfxWarning() << "Failed to create newSrcTexture: " << gfx::hexa(hr);
    return false;
  }

  RefPtr<layers::VideoProcessorD3D11> videoProcessor =
      layers::VideoProcessorD3D11::Create(device);
  if (!videoProcessor) {
    gfxWarning() << "Failed to create VideoProcessorD3D11";
    return false;
  }

  hr = videoProcessor->Init(aDestCpuTexture->GetSize());
  if (FAILED(hr)) {
    gfxWarning() << "Failed to init VideoProcessorD3D11" << gfx::hexa(hr);
    return false;
  }

  layers::VideoProcessorD3D11::InputTextureInfo info(
      aColorSpace, aColorRange, aTransferFunction, aArrayIndex, aSrcTexture);
  if (!videoProcessor->CallVideoProcessorBlt(info, newSrcTexture)) {
    gfxWarning() << "CallVideoProcessorBlt failed";
    return false;
  }

  return ReadbackTexture(aDestCpuTexture, newSrcTexture, 0, aColorSpace,
                         aColorRange, aTransferFunction);
}

/* static */
bool Factory::ReadbackTexture(DataSourceSurface* aDestCpuTexture,
                              ID3D11Texture2D* aSrcTexture,
                              uint32_t aArrayIndex,
                              gfx::ColorSpace2 aColorSpace,
                              gfx::ColorRange aColorRange,
                              gfx::TransferFunction aTransferFunction) {
  D3D11_TEXTURE2D_DESC srcDesc = {0};
  aSrcTexture->GetDesc(&srcDesc);

  // Special case: If the source and destination have different formats and the
  // destination is B8G8R8A8 then convert the source to B8G8R8A8 and readback.
  if ((srcDesc.Format != DXGIFormat(aDestCpuTexture->GetFormat())) &&
      (aDestCpuTexture->GetFormat() == SurfaceFormat::B8G8R8A8)) {
    return ConvertSourceAndRetryReadback(aDestCpuTexture, aSrcTexture,
                                         aArrayIndex, aColorSpace, aColorRange,
                                         aTransferFunction);
  }

  if ((IntSize(srcDesc.Width, srcDesc.Height) != aDestCpuTexture->GetSize()) ||
      (srcDesc.Format != DXGIFormat(aDestCpuTexture->GetFormat()))) {
    gfxWarning() << "Attempted readback between incompatible textures";
    return false;
  }

  gfx::DataSourceSurface::MappedSurface mappedSurface;
  if (!aDestCpuTexture->Map(gfx::DataSourceSurface::WRITE, &mappedSurface)) {
    return false;
  }

  MOZ_ASSERT(aArrayIndex == 0);

  bool ret =
      ReadbackTexture(mappedSurface.mData, mappedSurface.mStride, aSrcTexture);
  aDestCpuTexture->Unmap();
  return ret;
}

/* static */
bool Factory::ReadbackTexture(uint8_t* aDestData, int32_t aDestStride,
                              ID3D11Texture2D* aSrcTexture) {
  MOZ_ASSERT(aDestData && aDestStride && aSrcTexture);

  RefPtr<ID3D11Device> device;
  aSrcTexture->GetDevice(getter_AddRefs(device));
  if (!device) {
    gfxWarning() << "Failed to get D3D11 device from source texture";
    return false;
  }

  RefPtr<ID3D11DeviceContext> context;
  device->GetImmediateContext(getter_AddRefs(context));
  if (!context) {
    gfxWarning() << "Could not get an immediate D3D11 context";
    return false;
  }

  RefPtr<IDXGIKeyedMutex> mutex;
  HRESULT hr = aSrcTexture->QueryInterface(__uuidof(IDXGIKeyedMutex),
                                           (void**)getter_AddRefs(mutex));
  if (SUCCEEDED(hr) && mutex) {
    hr = mutex->AcquireSync(0, 2000);
    if (hr != S_OK) {
      gfxWarning() << "Could not acquire DXGI surface lock in 2 seconds";
      return false;
    }
  }

  D3D11_TEXTURE2D_DESC srcDesc = {0};
  aSrcTexture->GetDesc(&srcDesc);
  srcDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  srcDesc.Usage = D3D11_USAGE_STAGING;
  srcDesc.BindFlags = 0;
  srcDesc.MiscFlags = 0;
  srcDesc.MipLevels = 1;
  RefPtr<ID3D11Texture2D> srcCpuTexture;
  hr =
      device->CreateTexture2D(&srcDesc, nullptr, getter_AddRefs(srcCpuTexture));
  if (FAILED(hr)) {
    gfxWarning() << "Could not create source texture for mapping";
    if (mutex) {
      mutex->ReleaseSync(0);
    }
    return false;
  }

  context->CopyResource(srcCpuTexture, aSrcTexture);

  if (mutex) {
    mutex->ReleaseSync(0);
    mutex = nullptr;
  }

  D3D11_MAPPED_SUBRESOURCE srcMap;
  hr = context->Map(srcCpuTexture, 0, D3D11_MAP_READ, 0, &srcMap);
  if (FAILED(hr)) {
    gfxWarning() << "Could not map source texture";
    return false;
  }

  uint32_t width = srcDesc.Width;
  uint32_t height = srcDesc.Height;
  int bpp = BytesPerPixel(gfx::ToPixelFormat(srcDesc.Format));
  for (uint32_t y = 0; y < height; y++) {
    memcpy(aDestData + aDestStride * y,
           (unsigned char*)(srcMap.pData) + srcMap.RowPitch * y, width * bpp);
  }

  context->Unmap(srcCpuTexture, 0);
  return true;
}

#endif  // WIN32

// static
void CriticalLogger::OutputMessage(const std::string& aString, int aLevel,
                                   bool aNoNewline) {
  if (Factory::GetLogForwarder()) {
    Factory::GetLogForwarder()->Log(aString);
  }

  BasicLogger::OutputMessage(aString, aLevel, aNoNewline);
}

void CriticalLogger::CrashAction(LogReason aReason) {
  if (Factory::GetLogForwarder()) {
    Factory::GetLogForwarder()->CrashAction(aReason);
  }
}

#ifdef WIN32
void LogWStr(const wchar_t* aWStr, std::stringstream& aOut) {
  int n =
      WideCharToMultiByte(CP_ACP, 0, aWStr, -1, nullptr, 0, nullptr, nullptr);
  if (n > 1) {
    std::vector<char> str(n);
    WideCharToMultiByte(CP_ACP, 0, aWStr, -1, str.data(), n, nullptr, nullptr);
    aOut << str.data();
  }
}
#endif

}  // namespace mozilla::gfx
