/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageMemoryReporter.h"
#include "Image.h"
#include "base/process_util.h"
#include "mozilla/ipc/SharedMemoryMapping.h"
#include "mozilla/layers/SharedSurfacesParent.h"
#include "mozilla/StaticPrefs_image.h"
#include "nsIMemoryReporter.h"
#include "nsISupportsImpl.h"

namespace mozilla {
namespace image {

ImageMemoryReporter::WebRenderReporter* ImageMemoryReporter::sWrReporter;

class ImageMemoryReporter::WebRenderReporter final : public nsIMemoryReporter {
 public:
  NS_DECL_ISUPPORTS

  WebRenderReporter() = default;

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override {
    layers::SharedSurfacesMemoryReport report;
    layers::SharedSurfacesParent::AccumulateMemoryReport(report);
    ReportSharedSurfaces(aHandleReport, aData, /* aIsForCompositor */ true,
                         report);
    return NS_OK;
  }

 private:
  virtual ~WebRenderReporter() = default;
};

NS_IMPL_ISUPPORTS(ImageMemoryReporter::WebRenderReporter, nsIMemoryReporter)

/* static */
void ImageMemoryReporter::InitForWebRender() {
  MOZ_ASSERT(XRE_IsParentProcess() || XRE_IsGPUProcess());
  if (!sWrReporter) {
    sWrReporter = new WebRenderReporter();
    RegisterStrongMemoryReporter(do_AddRef(sWrReporter));
  }
}

/* static */
void ImageMemoryReporter::ShutdownForWebRender() {
  MOZ_ASSERT(XRE_IsParentProcess() || XRE_IsGPUProcess());
  if (sWrReporter) {
    UnregisterStrongMemoryReporter(sWrReporter);
    sWrReporter = nullptr;
  }
}

/* static */
void ImageMemoryReporter::ReportSharedSurfaces(
    nsIHandleReportCallback* aHandleReport, nsISupports* aData,
    const layers::SharedSurfacesMemoryReport& aSharedSurfaces) {
  ReportSharedSurfaces(aHandleReport, aData,
                       /* aIsForCompositor */ false, aSharedSurfaces);
}

/* static */
void ImageMemoryReporter::ReportSharedSurfaces(
    nsIHandleReportCallback* aHandleReport, nsISupports* aData,
    bool aIsForCompositor,
    const layers::SharedSurfacesMemoryReport& aSharedSurfaces) {
  MOZ_ASSERT_IF(aIsForCompositor, XRE_IsParentProcess() || XRE_IsGPUProcess());
  MOZ_ASSERT_IF(!aIsForCompositor,
                XRE_IsParentProcess() || XRE_IsContentProcess());

  for (auto i = aSharedSurfaces.mSurfaces.begin();
       i != aSharedSurfaces.mSurfaces.end(); ++i) {
    ReportSharedSurface(aHandleReport, aData, aIsForCompositor, i->first,
                        i->second);
  }
}

/* static */
void ImageMemoryReporter::ReportSharedSurface(
    nsIHandleReportCallback* aHandleReport, nsISupports* aData,
    bool aIsForCompositor, uint64_t aExternalId,
    const layers::SharedSurfacesMemoryReport::SurfaceEntry& aEntry) {
  nsAutoCString path;
  if (aIsForCompositor) {
    path.AppendLiteral("gfx/webrender/images/mapped_from_owner/");
  } else {
    path.AppendLiteral("gfx/webrender/images/owner_cache_missing/");
  }

  if (aIsForCompositor) {
    path.AppendLiteral("pid=");
    path.AppendInt(uint32_t(aEntry.mCreatorPid));
    path.AppendLiteral("/");
  }

  if (StaticPrefs::image_mem_debug_reporting()) {
    path.AppendInt(aExternalId, 16);
    path.AppendLiteral("/");
  }

  path.AppendLiteral("image(");
  path.AppendInt(aEntry.mSize.width);
  path.AppendLiteral("x");
  path.AppendInt(aEntry.mSize.height);
  path.AppendLiteral(", compositor_ref:");
  path.AppendInt(aEntry.mConsumers);
  path.AppendLiteral(", creator_ref:");
  path.AppendInt(aEntry.mCreatorRef);
  path.AppendLiteral(")/decoded-");

  size_t surfaceSize = mozilla::ipc::shared_memory::PageAlignedSize(
      aEntry.mSize.height * aEntry.mStride);

  // If this memory has already been reported elsewhere (e.g. as part of our
  // explicit section in the surface cache), we don't want report it again as
  // KIND_NONHEAP and have it counted again. The paths must be different if the
  // kinds are different to avoid problems when diffing memory reports.
  bool sameProcess = aEntry.mCreatorPid == base::GetCurrentProcId();
  int32_t kind;
  if (aIsForCompositor && !sameProcess) {
    path.AppendLiteral("nonheap");
    kind = nsIMemoryReporter::KIND_NONHEAP;
  } else {
    path.AppendLiteral("other");
    kind = nsIMemoryReporter::KIND_OTHER;
  }

  constexpr auto desc = "Decoded image data stored in shared memory."_ns;
  aHandleReport->Callback(""_ns, path, kind, nsIMemoryReporter::UNITS_BYTES,
                          surfaceSize, desc, aData);
}

/* static */
void ImageMemoryReporter::AppendSharedSurfacePrefix(
    nsACString& aPathPrefix, const SurfaceMemoryCounter& aCounter,
    layers::SharedSurfacesMemoryReport& aSharedSurfaces) {
  uint64_t extId = aCounter.Values().ExternalId();
  if (extId) {
    auto gpuEntry = aSharedSurfaces.mSurfaces.find(extId);

    if (StaticPrefs::image_mem_debug_reporting()) {
      aPathPrefix.AppendLiteral(", external_id:");
      aPathPrefix.AppendInt(extId, 16);
      if (gpuEntry != aSharedSurfaces.mSurfaces.end()) {
        aPathPrefix.AppendLiteral(", compositor_ref:");
        aPathPrefix.AppendInt(gpuEntry->second.mConsumers);
      } else {
        aPathPrefix.AppendLiteral(", compositor_ref:missing");
      }
    }

    if (gpuEntry != aSharedSurfaces.mSurfaces.end()) {
      MOZ_ASSERT(gpuEntry->second.mCreatorRef);
      aSharedSurfaces.mSurfaces.erase(gpuEntry);
    }
  }
}

/* static */
void ImageMemoryReporter::TrimSharedSurfaces(
    const ImageMemoryCounter& aCounter,
    layers::SharedSurfacesMemoryReport& aSharedSurfaces) {
  if (aSharedSurfaces.mSurfaces.empty()) {
    return;
  }

  for (const SurfaceMemoryCounter& counter : aCounter.Surfaces()) {
    uint64_t extId = counter.Values().ExternalId();
    if (extId) {
      auto gpuEntry = aSharedSurfaces.mSurfaces.find(extId);
      if (gpuEntry != aSharedSurfaces.mSurfaces.end()) {
        MOZ_ASSERT(gpuEntry->second.mCreatorRef);
        aSharedSurfaces.mSurfaces.erase(gpuEntry);
      }
    }
  }
}

}  // namespace image
}  // namespace mozilla
