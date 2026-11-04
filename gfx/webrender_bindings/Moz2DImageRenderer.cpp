/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <memory>
#include <unordered_map>

#include "GeckoProfiler.h"
#include "WebRenderTypes.h"
#include "gfxUtils.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/Mutex.h"
#include "mozilla/Range.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/RecordedEvent.h"
#include "mozilla/gfx/RectAbsolute.h"
#include "mozilla/layers/WebRenderDrawEventRecorder.h"
#include "webrender_ffi.h"
#ifdef FUZZING
#  include "prenv.h"
#endif

#ifdef XP_DARWIN
#  include "mozilla/gfx/UnscaledFontMac.h"
#elif defined(XP_WIN)
#  include "mozilla/gfx/UnscaledFontDWrite.h"
#else
#  include "mozilla/gfx/UnscaledFontFreeType.h"
#endif

namespace std {
template <>
struct hash<mozilla::wr::FontKey> {
  size_t operator()(const mozilla::wr::FontKey& key) const {
    return hash<size_t>()(mozilla::wr::AsUint64(key));
  }
};

template <>
struct hash<mozilla::wr::FontInstanceKey> {
  size_t operator()(const mozilla::wr::FontInstanceKey& key) const {
    return hash<size_t>()(mozilla::wr::AsUint64(key));
  }
};
};  // namespace std

namespace mozilla {

using namespace gfx;

namespace wr {

struct FontTemplate {
  const uint8_t* mData;
  size_t mSize;
  uint32_t mIndex;
  const VecU8* mVec;
  RefPtr<UnscaledFont> mUnscaledFont;

  FontTemplate() : mData(nullptr), mSize(0), mIndex(0), mVec(nullptr) {}

  ~FontTemplate() {
    if (mVec) {
      wr_dec_ref_arc(mVec);
    }
  }
};

struct FontInstanceData {
  WrFontKey mFontKey{};
  float mSize;
  Maybe<FontInstanceOptions> mOptions;
  Maybe<FontInstancePlatformOptions> mPlatformOptions;
  UniquePtr<wr::FontVariation[]> mVariations;
  size_t mNumVariations;
  RefPtr<ScaledFont> mScaledFont;

  FontInstanceData() : mSize(0), mNumVariations(0) {}
};

StaticMutex sFontDataTableLock;
MOZ_RUNINIT std::unordered_map<WrFontKey, FontTemplate> sFontDataTable;
MOZ_RUNINIT std::unordered_map<WrFontInstanceKey, FontInstanceData>
    sBlobFontTable;

// Fixed-size ring buffer logging font deletion events to aid debugging.
static struct FontDeleteLog {
  static const size_t MAX_ENTRIES = 256;

  uint64_t mEntries[MAX_ENTRIES] = {0};
  size_t mNextEntry = 0;

  void AddEntry(uint64_t aEntry) {
    mEntries[mNextEntry] = aEntry;
    mNextEntry = (mNextEntry + 1) % MAX_ENTRIES;
  }

  void Add(WrFontKey aKey) { AddEntry(AsUint64(aKey)); }

  // Store namespace clears as font id 0, since this will never be allocated.
  void Add(WrIdNamespace aNamespace) {
    AddEntry(AsUint64(WrFontKey{aNamespace, 0}));
  }

  void AddAll() { AddEntry(~0); }

  // Find a matching entry in the log, searching backwards starting at the
  // newest entry and finishing with the oldest entry. Returns a brief
  // description of why the font was deleted, if known.
  const char* Find(WrFontKey aKey) {
    uint64_t keyEntry = AsUint64(aKey);
    uint64_t namespaceEntry = AsUint64(WrFontKey{aKey.mNamespace, 0});
    size_t offset = mNextEntry;
    do {
      offset = (offset + MAX_ENTRIES - 1) % MAX_ENTRIES;
      if (mEntries[offset] == keyEntry) {
        return "deleted font";
      } else if (mEntries[offset] == namespaceEntry) {
        return "cleared namespace";
      } else if (mEntries[offset] == (uint64_t)~0) {
        return "cleared all";
      }
    } while (offset != mNextEntry);
    return "unknown font";
  }
} sFontDeleteLog;

void ClearAllBlobImageResources() {
  StaticMutexAutoLock lock(sFontDataTableLock);
  sFontDeleteLog.AddAll();
  sBlobFontTable.clear();
  sFontDataTable.clear();
}

extern "C" {
void ClearBlobImageResources(WrIdNamespace aNamespace) {
  StaticMutexAutoLock lock(sFontDataTableLock);
  sFontDeleteLog.Add(aNamespace);
  for (auto i = sBlobFontTable.begin(); i != sBlobFontTable.end();) {
    if (i->first.mNamespace == aNamespace) {
      i = sBlobFontTable.erase(i);
    } else {
      i++;
    }
  }
  for (auto i = sFontDataTable.begin(); i != sFontDataTable.end();) {
    if (i->first.mNamespace == aNamespace) {
      i = sFontDataTable.erase(i);
    } else {
      i++;
    }
  }
}

bool HasFontData(WrFontKey aKey) {
  StaticMutexAutoLock lock(sFontDataTableLock);
  return sFontDataTable.find(aKey) != sFontDataTable.end();
}

void AddFontData(WrFontKey aKey, const uint8_t* aData, size_t aSize,
                 uint32_t aIndex, const ArcVecU8* aVec) {
  StaticMutexAutoLock lock(sFontDataTableLock);
  auto i = sFontDataTable.find(aKey);
  if (i == sFontDataTable.end()) {
    FontTemplate& font = sFontDataTable[aKey];
    font.mData = aData;
    font.mSize = aSize;
    font.mIndex = aIndex;
    font.mVec = wr_add_ref_arc(aVec);
  }
}

void AddNativeFontHandle(WrFontKey aKey, void* aHandle, size_t aSize,
                         uint32_t aIndex) {
  StaticMutexAutoLock lock(sFontDataTableLock);
  auto i = sFontDataTable.find(aKey);
  if (i == sFontDataTable.end()) {
    FontTemplate& font = sFontDataTable[aKey];
#ifdef XP_DARWIN
    font.mUnscaledFont =
        new UnscaledFontMac(reinterpret_cast<CGFontRef>(aHandle), false);
#elif defined(XP_WIN)
    font.mUnscaledFont = new UnscaledFontDWrite(
        reinterpret_cast<IDWriteFontFace*>(aHandle), nullptr);
#else
    CheckedUint32 size(aSize);
    if (!size.isValid()) {
      return;
    }
#  ifdef ANDROID
    FontType type = FontType::FREETYPE;
#  else
    FontType type = FontType::FONTCONFIG;
#  endif
    font.mUnscaledFont = Factory::CreateUnscaledFontFromFontDescriptor(
        type, reinterpret_cast<const uint8_t*>(aHandle), size.value(), aIndex);
#endif
  }
}

void DeleteFontData(WrFontKey aKey) {
  StaticMutexAutoLock lock(sFontDataTableLock);
  sFontDeleteLog.Add(aKey);
  auto i = sFontDataTable.find(aKey);
  if (i != sFontDataTable.end()) {
    sFontDataTable.erase(i);
  }
}

void AddBlobFont(WrFontInstanceKey aInstanceKey, WrFontKey aFontKey,
                 float aSize, const FontInstanceOptions* aOptions,
                 const FontInstancePlatformOptions* aPlatformOptions,
                 const wr::FontVariation* aVariations, size_t aNumVariations) {
  StaticMutexAutoLock lock(sFontDataTableLock);
  auto i = sBlobFontTable.find(aInstanceKey);
  if (i == sBlobFontTable.end()) {
    FontInstanceData& font = sBlobFontTable[aInstanceKey];
    font.mFontKey = aFontKey;
    font.mSize = aSize;
    if (aOptions) {
      font.mOptions = Some(*aOptions);
    }
    if (aPlatformOptions) {
      font.mPlatformOptions = Some(*aPlatformOptions);
    }
    if (aNumVariations) {
      font.mNumVariations = aNumVariations;
      font.mVariations = std::make_unique<wr::FontVariation[]>(aNumVariations);
      PodCopy(font.mVariations.get(),
              reinterpret_cast<const wr::FontVariation*>(aVariations),
              aNumVariations);
    }
  }
}

void DeleteBlobFont(WrFontInstanceKey aKey) {
  StaticMutexAutoLock lock(sFontDataTableLock);
  auto i = sBlobFontTable.find(aKey);
  if (i != sBlobFontTable.end()) {
    sBlobFontTable.erase(i);
  }
}

}  // extern

static RefPtr<UnscaledFont> GetUnscaledFont(Translator* aTranslator,
                                            WrFontKey aKey) {
  auto i = sFontDataTable.find(aKey);
  if (i == sFontDataTable.end()) {
    gfxDevCrash(LogReason::UnscaledFontNotFound)
        << "Failed to get UnscaledFont entry for FontKey " << aKey.mHandle
        << " because " << sFontDeleteLog.Find(aKey);
    return nullptr;
  }
  FontTemplate& data = i->second;
  if (data.mUnscaledFont) {
    return data.mUnscaledFont;
  }
  MOZ_ASSERT(data.mData);
  FontType type =
#ifdef XP_DARWIN
      FontType::MAC;
#elif defined(XP_WIN)
      FontType::DWRITE;
#elif defined(ANDROID)
      FontType::FREETYPE;
#else
      FontType::FONTCONFIG;
#endif
  // makes a copy of the data
  CheckedUint32 size(data.mSize);
  RefPtr<NativeFontResource> fontResource;
  if (size.isValid()) {
    fontResource =
        Factory::CreateNativeFontResource((uint8_t*)data.mData, size.value(),
                                          type, aTranslator->GetFontContext());
  }
  RefPtr<UnscaledFont> unscaledFont;
  if (!fontResource) {
    gfxDevCrash(LogReason::NativeFontResourceNotFound)
        << "Failed to create NativeFontResource for FontKey " << aKey.mHandle;
  } else {
    // Instance data is only needed for GDI fonts which webrender does not
    // support.
    unscaledFont = fontResource->CreateUnscaledFont(data.mIndex, nullptr, 0);
    if (!unscaledFont) {
      gfxDevCrash(LogReason::UnscaledFontNotFound)
          << "Failed to create UnscaledFont for FontKey " << aKey.mHandle;
    }
  }
  data.mUnscaledFont = unscaledFont;
  return unscaledFont;
}

static RefPtr<ScaledFont> GetScaledFont(Translator* aTranslator,
                                        WrFontInstanceKey aKey) {
  StaticMutexAutoLock lock(sFontDataTableLock);
  auto i = sBlobFontTable.find(aKey);
  if (i == sBlobFontTable.end()) {
    gfxDevCrash(LogReason::ScaledFontNotFound)
        << "Failed to get ScaledFont entry for FontInstanceKey "
        << aKey.mHandle;
    return nullptr;
  }
  FontInstanceData& data = i->second;
  if (data.mScaledFont) {
    return data.mScaledFont;
  }
  RefPtr<UnscaledFont> unscaled = GetUnscaledFont(aTranslator, data.mFontKey);
  if (!unscaled) {
    return nullptr;
  }
  RefPtr<ScaledFont> scaled = unscaled->CreateScaledFontFromWRFont(
      data.mSize, data.mOptions.ptrOr(nullptr),
      data.mPlatformOptions.ptrOr(nullptr), data.mVariations.get(),
      data.mNumVariations);
  if (!scaled) {
    gfxDevCrash(LogReason::ScaledFontNotFound)
        << "Failed to create ScaledFont for FontKey " << aKey.mHandle;
  }
  data.mScaledFont = scaled;
  return data.mScaledFont;
}

template <typename T>
T ConvertFromBytes(const uint8_t* bytes) {
  T t;
  memcpy(&t, bytes, sizeof(T));
  return t;
}

struct Reader {
  const uint8_t* buf;
  size_t len;
  size_t pos;
#ifdef FUZZING
  bool ok = true;
#endif

  Reader(const uint8_t* buf, size_t len) : buf(buf), len(len), pos(0) {}

  template <typename T>
  T Read() {
#ifdef FUZZING
    if (pos + sizeof(T) > len) {
      ok = false;
      return T{};
    }
#else
    MOZ_RELEASE_ASSERT(pos + sizeof(T) <= len);
#endif
    T ret = ConvertFromBytes<T>(buf + pos);
    pos += sizeof(T);
    return ret;
  }

  size_t ReadSize() { return Read<size_t>(); }
  int ReadInt() { return Read<int>(); }

  IntRectAbsolute ReadBounds() { return Read<IntRectAbsolute>(); }

  layers::BlobFont ReadBlobFont() { return Read<layers::BlobFont>(); }
};

static bool Moz2DRenderCallback(const Range<const uint8_t> aBlob,
                                const mozilla::wr::IdNamespace& aIdNamespace,
                                gfx::SurfaceFormat aFormat,
                                const mozilla::wr::DeviceIntRect* aVisibleRect,
                                const mozilla::wr::LayoutIntRect* aRenderRect,
                                const uint16_t aTileSize,
                                const mozilla::wr::TileOffset* aTileOffset,
                                const mozilla::wr::LayoutIntRect* aDirtyRect,
                                Range<uint8_t> aOutput) {
  IntSize size(aRenderRect->width(), aRenderRect->height());
  AUTO_PROFILER_MARKER("RasterizeSingleBlob", GRAPHICS);
#ifndef FUZZING
  MOZ_RELEASE_ASSERT(size.width > 0 && size.height > 0);
#endif
  if (size.width <= 0 || size.height <= 0) {
    return false;
  }

  auto stride = size.width * gfx::BytesPerPixel(aFormat);

  if (aOutput.length() < static_cast<size_t>(size.height * stride)) {
    return false;
  }

  // In bindings.rs we allocate a buffer filled with opaque white.
  bool uninitialized = false;

  RefPtr<gfx::DrawTarget> dt = gfx::Factory::CreateDrawTargetForData(
      gfx::BackendType::SKIA, aOutput.begin().get(), size, stride, aFormat,
      uninitialized);

  if (!dt) {
    return false;
  }

  // aRenderRect is the part of the blob that we are currently rendering
  // (for example a tile) in the same coordinate space as aVisibleRect.
  IntPoint origin = gfx::IntPoint(aRenderRect->min.x, aRenderRect->min.y);

  dt = gfx::Factory::CreateOffsetDrawTarget(dt, origin);
  if (!dt) {
    return false;
  }

  // We try hard to not have empty blobs but we can end up with
  // them because of CompositorHitTestInfo and merging.
  size_t footerSize = sizeof(size_t);
#ifdef FUZZING
  if (aBlob.length() < footerSize) {
    return false;
  }
#else
  MOZ_RELEASE_ASSERT(aBlob.length() >= footerSize);
#endif
  size_t indexOffset = ConvertFromBytes<size_t>(aBlob.end().get() - footerSize);

#ifdef FUZZING
  if (indexOffset > aBlob.length() - footerSize) {
    return false;
  }
#else
  MOZ_RELEASE_ASSERT(indexOffset <= aBlob.length() - footerSize);
#endif
  Reader reader(aBlob.begin().get() + indexOffset,
                aBlob.length() - footerSize - indexOffset);

  auto bounds = gfx::IntRect(origin, size);

  if (aDirtyRect) {
    gfx::Rect dirty(aDirtyRect->min.x, aDirtyRect->min.y, aDirtyRect->width(),
                    aDirtyRect->height());
    dt->PushClipRect(dirty);
    bounds =
        bounds.Intersect(IntRect(aDirtyRect->min.x, aDirtyRect->min.y,
                                 aDirtyRect->width(), aDirtyRect->height()));
  }

  bool ret = true;
  size_t offset = 0;
  auto absBounds = IntRectAbsolute::FromRect(bounds);
  while (reader.pos < reader.len) {
    size_t end = reader.ReadSize();
    size_t extra_end = reader.ReadSize();
#ifdef FUZZING
    if (!reader.ok) {
      return false;
    }
    if (offset > end || extra_end < end || extra_end >= aBlob.length()) {
      return false;
    }
#else
    MOZ_RELEASE_ASSERT(offset <= end);
    MOZ_RELEASE_ASSERT(extra_end >= end);
    MOZ_RELEASE_ASSERT(extra_end < aBlob.length());
#endif

    auto combinedBounds = absBounds.Intersect(reader.ReadBounds());
#ifdef FUZZING
    if (!reader.ok) {
      return false;
    }
#endif
    if (combinedBounds.IsEmpty()) {
      offset = extra_end;
      continue;
    }

    layers::WebRenderTranslator translator(dt, aIdNamespace);
    Reader fontReader(aBlob.begin().get() + end, extra_end - end);
    size_t count = fontReader.ReadSize();
#ifdef FUZZING
    if (!fontReader.ok) {
      return false;
    }
#endif
    for (size_t i = 0; i < count; i++) {
      layers::BlobFont blobFont = fontReader.ReadBlobFont();
#ifdef FUZZING
      if (!fontReader.ok) {
        return false;
      }
#endif
      RefPtr<ScaledFont> scaledFont =
          GetScaledFont(&translator, blobFont.mFontInstanceKey);
      translator.AddScaledFont(blobFont.mScaledFontPtr, scaledFont);
    }

    Range<const uint8_t> blob(aBlob.begin() + offset, aBlob.begin() + end);
    ret =
        translator.TranslateRecording((char*)blob.begin().get(), blob.length());
    if (!ret) {
      gfxCriticalNote << "Replay failure: " << translator.GetError();
#ifdef FUZZING
      return false;
#else
      MOZ_RELEASE_ASSERT(false);
#endif
    }
    offset = extra_end;
  }

  if (StaticPrefs::gfx_webrender_debug_blob_paint_flashing()) {
    dt->SetTransform(gfx::Matrix());
    float r = float(rand()) / float(RAND_MAX);
    float g = float(rand()) / float(RAND_MAX);
    float b = float(rand()) / float(RAND_MAX);
    dt->FillRect(gfx::Rect(origin.x, origin.y, size.width, size.height),
                 gfx::ColorPattern(gfx::DeviceColor(r, g, b, 0.5)));
  }

  if (aDirtyRect) {
    dt->PopClip();
  }

#if 0
  static int i = 0;
  char filename[40];
  sprintf(filename, "out%d.png", i++);
  gfxUtils::WriteAsPNG(dt, filename);
#endif

  return ret;
}

}  // namespace wr
}  // namespace mozilla

#ifdef FUZZING
mozilla::StaticMutex sFuzzDumpLock;
static void writehex(const void* bytes, size_t len) {
  const uint8_t* ptr = static_cast<const uint8_t*>(bytes);
  while (len--) printf("%02X", *ptr++ & 0xFF);
}
#endif

extern "C" {

bool wr_moz2d_render_cb(const mozilla::wr::ByteSlice blob,
                        const mozilla::wr::IdNamespace aIdNamespace,
                        mozilla::wr::ImageFormat aFormat,
                        const mozilla::wr::LayoutIntRect* aRenderRect,
                        const mozilla::wr::DeviceIntRect* aVisibleRect,
                        const uint16_t aTileSize,
                        const mozilla::wr::TileOffset* aTileOffset,
                        const mozilla::wr::LayoutIntRect* aDirtyRect,
                        mozilla::wr::MutByteSlice output) {
#ifdef FUZZING
  if (!!PR_GetEnv("MOZ2D_RECORD")) {
    sFuzzDumpLock.Lock();
    printf("<dump>");
    writehex(&aFormat, sizeof(uint8_t));
    writehex(&aRenderRect->min.x, sizeof(int32_t));
    writehex(&aRenderRect->min.y, sizeof(int32_t));
    writehex(&aRenderRect->max.x, sizeof(int32_t));
    writehex(&aRenderRect->max.y, sizeof(int32_t));
    writehex(&aVisibleRect->min.x, sizeof(int32_t));
    writehex(&aVisibleRect->min.y, sizeof(int32_t));
    writehex(&aVisibleRect->max.x, sizeof(int32_t));
    writehex(&aVisibleRect->max.y, sizeof(int32_t));
    writehex(&aTileSize, sizeof(uint16_t));
    if (aTileSize) {
      writehex(&aTileOffset->x, sizeof(int32_t));
      writehex(&aTileOffset->y, sizeof(int32_t));
    }
    uint8_t byte = aDirtyRect ? 1 : 0;
    writehex(&byte, 1);
    if (byte) {
      writehex(&aDirtyRect->min.x, sizeof(int32_t));
      writehex(&aDirtyRect->min.y, sizeof(int32_t));
      writehex(&aDirtyRect->max.x, sizeof(int32_t));
      writehex(&aDirtyRect->max.y, sizeof(int32_t));
    }
    writehex(&output.len, sizeof(uint32_t));
    writehex(blob.buffer, blob.len);
    printf("</dump>\n");
    sFuzzDumpLock.Unlock();
  }
#endif

  return mozilla::wr::Moz2DRenderCallback(
      mozilla::wr::ByteSliceToRange(blob), aIdNamespace,
      mozilla::wr::ImageFormatToSurfaceFormat(aFormat), aVisibleRect,
      aRenderRect, aTileSize, aTileOffset, aDirtyRect,
      mozilla::wr::MutByteSliceToRange(output));
}

}  // extern
