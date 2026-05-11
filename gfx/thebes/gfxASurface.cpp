/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsIMemoryReporter.h"
#include "mozilla/Base64.h"
#include "mozilla/MemoryReporting.h"
#include "nsISupportsImpl.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/HelpersCairo.h"
#include "gfx2DGlue.h"

#include "gfxASurface.h"
#include "gfxContext.h"
#include "gfxImageSurface.h"
#include "gfxPlatform.h"
#include "gfxRect.h"

#include "cairo.h"

#ifdef CAIRO_HAS_WIN32_SURFACE
#  include "gfxWindowsSurface.h"
#endif

#ifdef MOZ_X11
#  include "gfxXlibSurface.h"
#endif

#ifdef CAIRO_HAS_QUARTZ_SURFACE
#  include "gfxQuartzSurface.h"
#endif

#include <stdio.h>
#include <limits.h>

#include "nsComponentManagerUtils.h"
#include "nsISupportsUtils.h"
#include "nsCOMPtr.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"

using namespace mozilla;
using namespace mozilla::gfx;

static cairo_user_data_key_t gfxasurface_pointer_key;

gfxASurface::gfxASurface()
    : mSurface(nullptr),
      mFloatingRefs(0),
      mBytesRecorded(0),
      mSurfaceValid(false) {
  MOZ_COUNT_CTOR(gfxASurface);
}

gfxASurface::~gfxASurface() {
  RecordMemoryFreed();

  MOZ_COUNT_DTOR(gfxASurface);
}

// Surfaces use refcounting that's tied to the cairo surface refcnt, to avoid
// refcount mismatch issues.
nsrefcnt gfxASurface::AddRef(void) {
  if (mSurfaceValid) {
    if (mFloatingRefs) {
      // eat a floating ref
      mFloatingRefs--;
    } else {
      cairo_surface_reference(mSurface);
    }

    return (nsrefcnt)cairo_surface_get_reference_count(mSurface);
  }
  // the surface isn't valid, but we still need to refcount
  // the gfxASurface
  return ++mFloatingRefs;
}

nsrefcnt gfxASurface::Release(void) {
  if (mSurfaceValid) {
    NS_ASSERTION(
        mFloatingRefs == 0,
        "gfxASurface::Release with floating refs still hanging around!");

    // Note that there is a destructor set on user data for mSurface,
    // which will delete this gfxASurface wrapper when the surface's refcount
    // goes out of scope.
    nsrefcnt refcnt = (nsrefcnt)cairo_surface_get_reference_count(mSurface);
    cairo_surface_destroy(mSurface);

    // |this| may not be valid any more, don't use it!

    return --refcnt;
  }
  if (--mFloatingRefs == 0) {
    delete this;
    return 0;
  }
  return mFloatingRefs;
}

void gfxASurface::SurfaceDestroyFunc(void* data) {
  gfxASurface* surf = (gfxASurface*)data;
  // fprintf (stderr, "Deleting wrapper for %p (wrapper: %p)\n", surf->mSurface,
  //          data);
  delete surf;
}

gfxASurface* gfxASurface::GetSurfaceWrapper(cairo_surface_t* csurf) {
  if (!csurf) return nullptr;
  return (gfxASurface*)cairo_surface_get_user_data(csurf,
                                                   &gfxasurface_pointer_key);
}

void gfxASurface::SetSurfaceWrapper(cairo_surface_t* csurf,
                                    gfxASurface* asurf) {
  if (!csurf) return;
  cairo_surface_set_user_data(csurf, &gfxasurface_pointer_key, asurf,
                              SurfaceDestroyFunc);
}

already_AddRefed<gfxASurface> gfxASurface::Wrap(cairo_surface_t* csurf,
                                                const IntSize& aSize) {
  RefPtr<gfxASurface> result;

  /* Do we already have a wrapper for this surface? */
  result = GetSurfaceWrapper(csurf);
  if (result) {
    // fprintf(stderr, "Existing wrapper for %p -> %p\n", csurf, result);
    return result.forget();
  }

  /* No wrapper; figure out the surface type and create it */
  cairo_surface_type_t stype = cairo_surface_get_type(csurf);

  if (stype == CAIRO_SURFACE_TYPE_IMAGE) {
    result = new gfxImageSurface(csurf);
  }
#ifdef CAIRO_HAS_WIN32_SURFACE
  else if (stype == CAIRO_SURFACE_TYPE_WIN32 ||
           stype == CAIRO_SURFACE_TYPE_WIN32_PRINTING) {
    result = new gfxWindowsSurface(csurf);
  }
#endif
#ifdef MOZ_X11
  else if (stype == CAIRO_SURFACE_TYPE_XLIB) {
    result = new gfxXlibSurface(csurf);
  }
#endif
#ifdef CAIRO_HAS_QUARTZ_SURFACE
  else if (stype == CAIRO_SURFACE_TYPE_QUARTZ) {
    result = new gfxQuartzSurface(csurf, aSize);
  }
#endif
  else {
    result = new gfxUnknownSurface(csurf, aSize);
  }

  // fprintf(stderr, "New wrapper for %p -> %p\n", csurf, result);

  return result.forget();
}

void gfxASurface::Init(cairo_surface_t* surface, bool existingSurface) {
  SetSurfaceWrapper(surface, this);
  MOZ_ASSERT(surface, "surface should be a valid pointer");

  mSurface = surface;
  mSurfaceValid = !cairo_surface_status(surface);
  if (!mSurfaceValid) {
    gfxWarning() << "ASurface Init failed with Cairo status "
                 << cairo_surface_status(surface) << " on " << hexa(surface);
  }

  if (existingSurface || !mSurfaceValid) {
    mFloatingRefs = 0;
  } else {
    mFloatingRefs = 1;
    if (cairo_surface_get_content(surface) != CAIRO_CONTENT_COLOR) {
      cairo_surface_set_subpixel_antialiasing(
          surface, CAIRO_SUBPIXEL_ANTIALIASING_DISABLED);
    }
  }
}

gfxSurfaceType gfxASurface::GetType() const {
  if (!mSurfaceValid) return (gfxSurfaceType)-1;

  return (gfxSurfaceType)cairo_surface_get_type(mSurface);
}

gfxContentType gfxASurface::GetContentType() const {
  if (!mSurfaceValid) return (gfxContentType)-1;

  return (gfxContentType)cairo_surface_get_content(mSurface);
}

void gfxASurface::SetDeviceOffset(const gfxPoint& offset) {
  if (!mSurfaceValid) return;
  cairo_surface_set_device_offset(mSurface, offset.x, offset.y);
}

gfxPoint gfxASurface::GetDeviceOffset() const {
  if (!mSurfaceValid) return gfxPoint(0.0, 0.0);
  gfxPoint pt;
  cairo_surface_get_device_offset(mSurface, &pt.x.value, &pt.y.value);
  return pt;
}

void gfxASurface::Flush() const {
  if (!mSurfaceValid) return;
  cairo_surface_flush(mSurface);
  gfxPlatform::ClearSourceSurfaceForSurface(const_cast<gfxASurface*>(this));
}

void gfxASurface::MarkDirty() {
  if (!mSurfaceValid) return;
  cairo_surface_mark_dirty(mSurface);
  gfxPlatform::ClearSourceSurfaceForSurface(this);
}

void gfxASurface::MarkDirty(const gfxRect& r) {
  if (!mSurfaceValid) return;
  cairo_surface_mark_dirty_rectangle(mSurface, (int)r.X(), (int)r.Y(),
                                     (int)r.Width(), (int)r.Height());
  gfxPlatform::ClearSourceSurfaceForSurface(this);
}

void gfxASurface::SetData(const cairo_user_data_key_t* key, void* user_data,
                          thebes_destroy_func_t destroy) {
  if (!mSurfaceValid) return;
  cairo_surface_set_user_data(mSurface, key, user_data, destroy);
}

void* gfxASurface::GetData(const cairo_user_data_key_t* key) {
  if (!mSurfaceValid) return nullptr;
  return cairo_surface_get_user_data(mSurface, key);
}

void gfxASurface::Finish() {
  // null surfaces are allowed here
  cairo_surface_finish(mSurface);
}

int gfxASurface::CairoStatus() {
  if (!mSurfaceValid) return -1;

  return cairo_surface_status(mSurface);
}

nsresult gfxASurface::BeginPrinting(const nsAString& aTitle,
                                    const nsAString& aPrintToFileName) {
  return NS_OK;
}

nsresult gfxASurface::EndPrinting() { return NS_OK; }

nsresult gfxASurface::AbortPrinting() { return NS_OK; }

nsresult gfxASurface::BeginPage() { return NS_OK; }

nsresult gfxASurface::EndPage() { return NS_OK; }

gfxContentType gfxASurface::ContentFromFormat(gfxImageFormat format) {
  switch (format) {
    case SurfaceFormat::A8R8G8B8_UINT32:
      return gfxContentType::COLOR_ALPHA;
    case SurfaceFormat::X8R8G8B8_UINT32:
    case SurfaceFormat::R5G6B5_UINT16:
      return gfxContentType::COLOR;
    case SurfaceFormat::A8:
      return gfxContentType::ALPHA;

    case SurfaceFormat::UNKNOWN:
    default:
      return gfxContentType::COLOR;
  }
}

int32_t gfxASurface::BytePerPixelFromFormat(gfxImageFormat format) {
  switch (format) {
    case SurfaceFormat::A8R8G8B8_UINT32:
    case SurfaceFormat::X8R8G8B8_UINT32:
      return 4;
    case SurfaceFormat::R5G6B5_UINT16:
      return 2;
    case SurfaceFormat::A8:
      return 1;
    default:
      NS_WARNING("Unknown byte per pixel value for Image format");
  }
  return 0;
}

/** Memory reporting **/

static const char* sDefaultSurfaceDescription =
    "Memory used by gfx surface of the given type.";

struct SurfaceMemoryReporterAttrs {
  const char* path;
  const char* description;
};

static const SurfaceMemoryReporterAttrs sSurfaceMemoryReporterAttrs[] = {
    {"gfx-surface-image", nullptr},
    {"gfx-surface-pdf", nullptr},
    {"gfx-surface-ps", nullptr},
    {"gfx-surface-xlib",
     "Memory used by xlib surfaces to store pixmaps. This memory lives in "
     "the X server's process rather than in this application, so the bytes "
     "accounted for here aren't counted in vsize, resident, explicit, or any "
     "of "
     "the other measurements on this page."},
    {"gfx-surface-xcb", nullptr},
    {"gfx-surface-glitz???", nullptr},  // should never be used
    {"gfx-surface-quartz", nullptr},
    {"gfx-surface-win32", nullptr},
    {"gfx-surface-beos", nullptr},
    {"gfx-surface-directfb???", nullptr},  // should never be used
    {"gfx-surface-svg", nullptr},
    {"gfx-surface-os2", nullptr},
    {"gfx-surface-win32printing", nullptr},
    {"gfx-surface-quartzimage", nullptr},
    {"gfx-surface-script", nullptr},
    {"gfx-surface-qpainter", nullptr},
    {"gfx-surface-recording", nullptr},
    {"gfx-surface-vg", nullptr},
    {"gfx-surface-gl", nullptr},
    {"gfx-surface-drm", nullptr},
    {"gfx-surface-tee", nullptr},
    {"gfx-surface-xml", nullptr},
    {"gfx-surface-skia", nullptr},
    {"gfx-surface-subsurface", nullptr},
};

static_assert(std::size(sSurfaceMemoryReporterAttrs) ==
                  size_t(gfxSurfaceType::Max),
              "sSurfaceMemoryReporterAttrs exceeds max capacity");
static_assert(uint32_t(CAIRO_SURFACE_TYPE_SKIA) ==
                  uint32_t(gfxSurfaceType::Skia),
              "CAIRO_SURFACE_TYPE_SKIA not equal to gfxSurfaceType::Skia");

/* Surface size memory reporting */

class SurfaceMemoryReporter final : public nsIMemoryReporter {
  ~SurfaceMemoryReporter() = default;

  // We can touch this array on several different threads, and we don't
  // want to introduce memory barriers when recording the memory used.  To
  // assure dynamic race checkers like TSan that this is OK, we use
  // relaxed memory ordering here.
  static Atomic<size_t, Relaxed>
      sSurfaceMemoryUsed[size_t(gfxSurfaceType::Max)];

 public:
  static void AdjustUsedMemory(gfxSurfaceType aType, int32_t aBytes) {
    // A read-modify-write operation like += would require a memory barrier
    // here, which would defeat the purpose of using relaxed memory
    // ordering.  So separate out the read and write operations.
    sSurfaceMemoryUsed[size_t(aType)] =
        sSurfaceMemoryUsed[size_t(aType)] + aBytes;
  };

  // This memory reporter is sometimes allocated on the compositor thread,
  // but always released on the main thread, so its refcounting needs to be
  // threadsafe.
  NS_DECL_THREADSAFE_ISUPPORTS

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override {
    const size_t len = std::size(sSurfaceMemoryReporterAttrs);
    for (size_t i = 0; i < len; i++) {
      int64_t amount = sSurfaceMemoryUsed[i];

      if (amount != 0) {
        const char* path = sSurfaceMemoryReporterAttrs[i].path;
        const char* desc = sSurfaceMemoryReporterAttrs[i].description;
        if (!desc) {
          desc = sDefaultSurfaceDescription;
        }

        aHandleReport->Callback(""_ns, nsCString(path), KIND_OTHER, UNITS_BYTES,
                                amount, nsCString(desc), aData);
      }
    }

    return NS_OK;
  }
};

Atomic<size_t, Relaxed>
    SurfaceMemoryReporter::sSurfaceMemoryUsed[size_t(gfxSurfaceType::Max)];

NS_IMPL_ISUPPORTS(SurfaceMemoryReporter, nsIMemoryReporter)

void gfxASurface::RecordMemoryUsedForSurfaceType(gfxSurfaceType aType,
                                                 int32_t aBytes) {
  if (int(aType) < 0 || aType >= gfxSurfaceType::Max) {
    NS_WARNING("Invalid type to RecordMemoryUsedForSurfaceType!");
    return;
  }

  static bool registered = false;
  if (!registered) {
    RegisterStrongMemoryReporter(MakeAndAddRef<SurfaceMemoryReporter>());
    registered = true;
  }

  SurfaceMemoryReporter::AdjustUsedMemory(aType, aBytes);
}

void gfxASurface::RecordMemoryUsed(int32_t aBytes) {
  RecordMemoryUsedForSurfaceType(GetType(), aBytes);
  mBytesRecorded += aBytes;
}

void gfxASurface::RecordMemoryFreed() {
  if (mBytesRecorded) {
    RecordMemoryUsedForSurfaceType(GetType(), -mBytesRecorded);
    mBytesRecorded = 0;
  }
}

size_t gfxASurface::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const {
  // We don't measure mSurface because cairo doesn't allow it.
  return 0;
}

size_t gfxASurface::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
  return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
}

/* static */
uint8_t gfxASurface::BytesPerPixel(gfxImageFormat aImageFormat) {
  switch (aImageFormat) {
    case SurfaceFormat::A8R8G8B8_UINT32:
      return 4;
    case SurfaceFormat::X8R8G8B8_UINT32:
      return 4;
    case SurfaceFormat::R5G6B5_UINT16:
      return 2;
    case SurfaceFormat::A8:
      return 1;
    case SurfaceFormat::UNKNOWN:
    default:
      MOZ_ASSERT_UNREACHABLE("Not really sure what you want me to say here");
      return 0;
  }
}

void gfxASurface::SetOpaqueRect(const gfxRect& aRect) {
  if (aRect.IsEmpty()) {
    mOpaqueRect = nullptr;
  } else if (!!mOpaqueRect) {
    *mOpaqueRect = aRect;
  } else {
    mOpaqueRect = MakeUnique<gfxRect>(aRect);
  }
}

/* static */ const gfxRect& gfxASurface::GetEmptyOpaqueRect() {
  static const gfxRect empty(0, 0, 0, 0);
  return empty;
}

const IntSize gfxASurface::GetSize() const { return IntSize(-1, -1); }

SurfaceFormat gfxASurface::GetSurfaceFormat() const {
  if (!mSurfaceValid) {
    return SurfaceFormat::UNKNOWN;
  }
  return GfxFormatForCairoSurface(mSurface);
}

already_AddRefed<gfxImageSurface> gfxASurface::GetAsImageSurface() {
  return nullptr;
}
