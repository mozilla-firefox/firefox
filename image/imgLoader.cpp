/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Undefine windows version of LoadImage because our code uses that name.
#include "mozilla/ScopeExit.h"
#include "nsIChildChannel.h"
#include "nsIThreadRetargetableStreamListener.h"
#undef LoadImage

#include "imgLoader.h"

#include <algorithm>
#include <utility>

#include "DecoderFactory.h"
#include "Image.h"
#include "ImageLogging.h"
#include "ReferrerInfo.h"
#include "imgRequestProxy.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/ChaosMode.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/StaticPrefs_image.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/Maybe.h"
#include "mozilla/SharedSubResourceCache.h"
#include "mozilla/dom/CacheExpirationTime.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/FetchPriority.h"
#include "mozilla/dom/nsMixedContentBlocker.h"
#ifdef NIGHTLY_BUILD
#  include "mozilla/dom/PolicyContainer.h"
#  include "mozilla/dom/IntegrityPolicyWAICT.h"
#  include "mozilla/dom/WAICTUtils.h"
#  include "mozilla/StaticPrefs_security.h"
#  include "nsStringStream.h"
static bool ShouldEnableWAICT(mozilla::dom::Document* aDoc);
#endif
#include "mozilla/image/ImageMemoryReporter.h"
#include "mozilla/layers/CompositorManagerChild.h"
#include "nsCOMPtr.h"
#include "nsCRT.h"
#include "nsComponentManagerUtils.h"
#include "nsContentPolicyUtils.h"
#include "nsContentSecurityManager.h"
#include "nsContentUtils.h"
#include "nsHttpChannel.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsICacheInfoChannel.h"
#include "nsIChannelEventSink.h"
#include "nsIClassOfService.h"
#include "nsIEffectiveTLDService.h"
#include "nsIFile.h"
#include "nsIFileURL.h"
#include "nsIHttpChannel.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIMemoryReporter.h"
#include "nsIProgressEventSink.h"
#include "nsIProtocolHandler.h"
#include "nsImageModule.h"
#include "nsMediaSniffer.h"
#include "nsMimeTypes.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsProxyRelease.h"
#include "nsQueryObject.h"
#include "nsReadableUtils.h"
#include "nsStreamUtils.h"
#include "prtime.h"

// we want to explore making the document own the load group
// so we can associate the document URI with the load group.
// until this point, we have an evil hack:
#include "nsIHttpChannelInternal.h"
#include "nsILoadGroupChild.h"
#include "nsIDocShell.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::image;
using namespace mozilla::net;

MOZ_DEFINE_MALLOC_SIZE_OF(ImagesMallocSizeOf)

class imgMemoryReporter final : public nsIMemoryReporter {
  ~imgMemoryReporter() = default;

 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override {
    MOZ_ASSERT(NS_IsMainThread());

    layers::CompositorManagerChild* manager =
        mozilla::layers::CompositorManagerChild::GetInstance();
    if (!manager || !StaticPrefs::image_mem_debug_reporting()) {
      layers::SharedSurfacesMemoryReport sharedSurfaces;
      FinishCollectReports(aHandleReport, aData, aAnonymize, sharedSurfaces);
      return NS_OK;
    }

    RefPtr<imgMemoryReporter> self(this);
    nsCOMPtr<nsIHandleReportCallback> handleReport(aHandleReport);
    nsCOMPtr<nsISupports> data(aData);
    manager->SendReportSharedSurfacesMemory(
        [=](layers::SharedSurfacesMemoryReport aReport) {
          self->FinishCollectReports(handleReport, data, aAnonymize, aReport);
        },
        [=](mozilla::ipc::ResponseRejectReason&& aReason) {
          layers::SharedSurfacesMemoryReport sharedSurfaces;
          self->FinishCollectReports(handleReport, data, aAnonymize,
                                     sharedSurfaces);
        });
    return NS_OK;
  }

  void FinishCollectReports(
      nsIHandleReportCallback* aHandleReport, nsISupports* aData,
      bool aAnonymize, layers::SharedSurfacesMemoryReport& aSharedSurfaces) {
    nsTArray<ImageMemoryCounter> chrome;
    nsTArray<ImageMemoryCounter> content;
    nsTArray<ImageMemoryCounter> uncached;

    for (uint32_t i = 0; i < mKnownLoaders.Length(); i++) {
      for (imgCacheEntry* entry : mKnownLoaders[i]->mCache.Values()) {
        RefPtr<imgRequest> req = entry->GetRequest();
        RecordCounterForRequest(req, &content, !entry->HasNoProxies());
      }
      MutexAutoLock lock(mKnownLoaders[i]->mUncachedImagesMutex);
      for (RefPtr<imgRequest> req : mKnownLoaders[i]->mUncachedImages) {
        RecordCounterForRequest(req, &uncached, req->HasConsumers());
      }
    }

    // Note that we only need to anonymize content image URIs.

    ReportCounterArray(aHandleReport, aData, chrome, "images/chrome",
                       /* aAnonymize */ false, aSharedSurfaces);

    ReportCounterArray(aHandleReport, aData, content, "images/content",
                       aAnonymize, aSharedSurfaces);

    // Uncached images may be content or chrome, so anonymize them.
    ReportCounterArray(aHandleReport, aData, uncached, "images/uncached",
                       aAnonymize, aSharedSurfaces);

    // Report any shared surfaces that were not merged with the surface cache.
    ImageMemoryReporter::ReportSharedSurfaces(aHandleReport, aData,
                                              aSharedSurfaces);

    nsCOMPtr<nsIMemoryReporterManager> imgr =
        do_GetService("@mozilla.org/memory-reporter-manager;1");
    if (imgr) {
      imgr->EndReport();
    }
  }

  static int64_t ImagesContentUsedUncompressedDistinguishedAmount() {
    size_t n = 0;
    for (uint32_t i = 0; i < imgLoader::sMemReporter->mKnownLoaders.Length();
         i++) {
      nsTArray<RefPtr<imgCacheEntry>> entries(
          imgLoader::sMemReporter->mKnownLoaders[i]->mCache.Count());

      for (imgCacheEntry* entry :
           imgLoader::sMemReporter->mKnownLoaders[i]->mCache.Values()) {
        entries.AppendElement(entry);
      }
      for (imgCacheEntry* entry : entries) {
        if (entry->HasNoProxies()) {
          continue;
        }

        RefPtr<imgRequest> req = entry->GetRequest();
        RefPtr<image::Image> image = req->GetImage();
        if (!image) {
          continue;
        }

        // Both this and EntryImageSizes measure
        // images/content/raster/used/decoded memory.  This function's
        // measurement is secondary -- the result doesn't go in the "explicit"
        // tree -- so we use moz_malloc_size_of instead of ImagesMallocSizeOf to
        // prevent DMD from seeing it reported twice.
        SizeOfState state(moz_malloc_size_of);
        ImageMemoryCounter counter(req, image, state, /* aIsUsed = */ true);

        n += counter.Values().DecodedHeap();
        n += counter.Values().DecodedNonHeap();
        n += counter.Values().DecodedUnknown();
      }
    }
    return n;
  }

  void RegisterLoader(imgLoader* aLoader) {
    mKnownLoaders.AppendElement(aLoader);
  }

  void UnregisterLoader(imgLoader* aLoader) {
    mKnownLoaders.RemoveElement(aLoader);
  }

 private:
  nsTArray<imgLoader*> mKnownLoaders;

  struct MemoryTotal {
    MemoryTotal& operator+=(const ImageMemoryCounter& aImageCounter) {
      if (aImageCounter.Type() == imgIContainer::TYPE_RASTER) {
        if (aImageCounter.IsUsed()) {
          mUsedRasterCounter += aImageCounter.Values();
        } else {
          mUnusedRasterCounter += aImageCounter.Values();
        }
      } else if (aImageCounter.Type() == imgIContainer::TYPE_VECTOR) {
        if (aImageCounter.IsUsed()) {
          mUsedVectorCounter += aImageCounter.Values();
        } else {
          mUnusedVectorCounter += aImageCounter.Values();
        }
      } else if (aImageCounter.Type() == imgIContainer::TYPE_REQUEST) {
        // Nothing to do, we did not get to the point of having an image.
      } else {
        MOZ_CRASH("Unexpected image type");
      }

      return *this;
    }

    const MemoryCounter& UsedRaster() const { return mUsedRasterCounter; }
    const MemoryCounter& UnusedRaster() const { return mUnusedRasterCounter; }
    const MemoryCounter& UsedVector() const { return mUsedVectorCounter; }
    const MemoryCounter& UnusedVector() const { return mUnusedVectorCounter; }

   private:
    MemoryCounter mUsedRasterCounter;
    MemoryCounter mUnusedRasterCounter;
    MemoryCounter mUsedVectorCounter;
    MemoryCounter mUnusedVectorCounter;
  };

  // Reports all images of a single kind, e.g. all used chrome images.
  void ReportCounterArray(nsIHandleReportCallback* aHandleReport,
                          nsISupports* aData,
                          nsTArray<ImageMemoryCounter>& aCounterArray,
                          const char* aPathPrefix, bool aAnonymize,
                          layers::SharedSurfacesMemoryReport& aSharedSurfaces) {
    MemoryTotal summaryTotal;
    MemoryTotal nonNotableTotal;

    // Report notable images, and compute total and non-notable aggregate sizes.
    for (uint32_t i = 0; i < aCounterArray.Length(); i++) {
      ImageMemoryCounter& counter = aCounterArray[i];

      if (aAnonymize) {
        counter.URI().Truncate();
        counter.URI().AppendPrintf("<anonymized-%u>", i);
      } else {
        // The URI could be an extremely long data: URI. Truncate if needed.
        static const size_t max = 256;
        if (counter.URI().Length() > max) {
          counter.URI().Truncate(max);
          counter.URI().AppendLiteral(" (truncated)");
        }
        counter.URI().ReplaceChar('/', '\\');
      }

      summaryTotal += counter;

      if (counter.IsNotable() || StaticPrefs::image_mem_debug_reporting()) {
        ReportImage(aHandleReport, aData, aPathPrefix, counter,
                    aSharedSurfaces);
      } else {
        ImageMemoryReporter::TrimSharedSurfaces(counter, aSharedSurfaces);
        nonNotableTotal += counter;
      }
    }

    // Report non-notable images in aggregate.
    ReportTotal(aHandleReport, aData, /* aExplicit = */ true, aPathPrefix,
                "<non-notable images>/", nonNotableTotal);

    // Report a summary in aggregate, outside of the explicit tree.
    ReportTotal(aHandleReport, aData, /* aExplicit = */ false, aPathPrefix, "",
                summaryTotal);
  }

  static void ReportImage(nsIHandleReportCallback* aHandleReport,
                          nsISupports* aData, const char* aPathPrefix,
                          const ImageMemoryCounter& aCounter,
                          layers::SharedSurfacesMemoryReport& aSharedSurfaces) {
    nsAutoCString pathPrefix("explicit/"_ns);
    pathPrefix.Append(aPathPrefix);

    switch (aCounter.Type()) {
      case imgIContainer::TYPE_RASTER:
        pathPrefix.AppendLiteral("/raster/");
        break;
      case imgIContainer::TYPE_VECTOR:
        pathPrefix.AppendLiteral("/vector/");
        break;
      case imgIContainer::TYPE_REQUEST:
        pathPrefix.AppendLiteral("/request/");
        break;
      default:
        pathPrefix.AppendLiteral("/unknown=");
        pathPrefix.AppendInt(aCounter.Type());
        pathPrefix.AppendLiteral("/");
        break;
    }

    pathPrefix.Append(aCounter.IsUsed() ? "used/" : "unused/");
    if (aCounter.IsValidating()) {
      pathPrefix.AppendLiteral("validating/");
    }
    if (aCounter.HasError()) {
      pathPrefix.AppendLiteral("err/");
    }

    pathPrefix.AppendLiteral("progress=");
    pathPrefix.AppendInt(aCounter.Progress(), 16);
    pathPrefix.AppendLiteral("/");

    pathPrefix.AppendLiteral("image(");
    pathPrefix.AppendInt(aCounter.IntrinsicSize().width);
    pathPrefix.AppendLiteral("x");
    pathPrefix.AppendInt(aCounter.IntrinsicSize().height);
    pathPrefix.AppendLiteral(", ");

    if (aCounter.URI().IsEmpty()) {
      pathPrefix.AppendLiteral("<unknown URI>");
    } else {
      pathPrefix.Append(aCounter.URI());
    }

    pathPrefix.AppendLiteral(")/");

    ReportSurfaces(aHandleReport, aData, pathPrefix, aCounter, aSharedSurfaces);

    ReportSourceValue(aHandleReport, aData, pathPrefix, aCounter.Values());
  }

  static void ReportSurfaces(
      nsIHandleReportCallback* aHandleReport, nsISupports* aData,
      const nsACString& aPathPrefix, const ImageMemoryCounter& aCounter,
      layers::SharedSurfacesMemoryReport& aSharedSurfaces) {
    for (const SurfaceMemoryCounter& counter : aCounter.Surfaces()) {
      nsAutoCString surfacePathPrefix(aPathPrefix);
      switch (counter.Type()) {
        case SurfaceMemoryCounterType::NORMAL:
          if (counter.IsLocked()) {
            surfacePathPrefix.AppendLiteral("locked/");
          } else {
            surfacePathPrefix.AppendLiteral("unlocked/");
          }
          if (counter.IsFactor2()) {
            surfacePathPrefix.AppendLiteral("factor2/");
          }
          if (counter.CannotSubstitute()) {
            surfacePathPrefix.AppendLiteral("cannot_substitute/");
          }
          break;
        case SurfaceMemoryCounterType::CONTAINER:
          surfacePathPrefix.AppendLiteral("container/");
          break;
        default:
          MOZ_ASSERT_UNREACHABLE("Unknown counter type");
          break;
      }

      surfacePathPrefix.AppendLiteral("types=");
      surfacePathPrefix.AppendInt(counter.Values().SurfaceTypes(), 16);
      surfacePathPrefix.AppendLiteral("/surface(");
      surfacePathPrefix.AppendInt(counter.Key().Size().width);
      surfacePathPrefix.AppendLiteral("x");
      surfacePathPrefix.AppendInt(counter.Key().Size().height);

      if (!counter.IsFinished()) {
        surfacePathPrefix.AppendLiteral(", incomplete");
      }

      if (counter.Values().ExternalHandles() > 0) {
        surfacePathPrefix.AppendLiteral(", handles:");
        surfacePathPrefix.AppendInt(
            uint32_t(counter.Values().ExternalHandles()));
      }

      ImageMemoryReporter::AppendSharedSurfacePrefix(surfacePathPrefix, counter,
                                                     aSharedSurfaces);

      PlaybackType playback = counter.Key().Playback();
      if (playback == PlaybackType::eAnimated) {
        if (StaticPrefs::image_mem_debug_reporting()) {
          surfacePathPrefix.AppendPrintf(
              " (animation %4u)", uint32_t(counter.Values().FrameIndex()));
        } else {
          surfacePathPrefix.AppendLiteral(" (animation)");
        }
      }

      if (counter.Key().Flags() != DefaultSurfaceFlags()) {
        surfacePathPrefix.AppendLiteral(", flags:");
        surfacePathPrefix.AppendInt(uint32_t(counter.Key().Flags()),
                                    /* aRadix = */ 16);
      }

      if (counter.Key().Region()) {
        const ImageIntRegion& region = counter.Key().Region().ref();
        const gfx::IntRect& rect = region.Rect();
        surfacePathPrefix.AppendLiteral(", region:[ rect=(");
        surfacePathPrefix.AppendInt(rect.x);
        surfacePathPrefix.AppendLiteral(",");
        surfacePathPrefix.AppendInt(rect.y);
        surfacePathPrefix.AppendLiteral(") ");
        surfacePathPrefix.AppendInt(rect.width);
        surfacePathPrefix.AppendLiteral("x");
        surfacePathPrefix.AppendInt(rect.height);
        if (region.IsRestricted()) {
          const gfx::IntRect& restrict = region.Restriction();
          if (restrict == rect) {
            surfacePathPrefix.AppendLiteral(", restrict=rect");
          } else {
            surfacePathPrefix.AppendLiteral(", restrict=(");
            surfacePathPrefix.AppendInt(restrict.x);
            surfacePathPrefix.AppendLiteral(",");
            surfacePathPrefix.AppendInt(restrict.y);
            surfacePathPrefix.AppendLiteral(") ");
            surfacePathPrefix.AppendInt(restrict.width);
            surfacePathPrefix.AppendLiteral("x");
            surfacePathPrefix.AppendInt(restrict.height);
          }
        }
        if (region.GetExtendMode() != gfx::ExtendMode::CLAMP) {
          surfacePathPrefix.AppendLiteral(", extendMode=");
          surfacePathPrefix.AppendInt(int32_t(region.GetExtendMode()));
        }
        surfacePathPrefix.AppendLiteral("]");
      }

      const SVGImageContext& context = counter.Key().SVGContext();
      surfacePathPrefix.AppendLiteral(", svgContext:[ ");
      if (context.GetViewportSize()) {
        const CSSIntSize& size = context.GetViewportSize().ref();
        surfacePathPrefix.AppendLiteral("viewport=(");
        surfacePathPrefix.AppendInt(size.width);
        surfacePathPrefix.AppendLiteral("x");
        surfacePathPrefix.AppendInt(size.height);
        surfacePathPrefix.AppendLiteral(") ");
      }
      if (context.GetPreserveAspectRatio()) {
        nsAutoString aspect;
        context.GetPreserveAspectRatio()->ToString(aspect);
        surfacePathPrefix.AppendLiteral("preserveAspectRatio=(");
        LossyAppendUTF16toASCII(aspect, surfacePathPrefix);
        surfacePathPrefix.AppendLiteral(") ");
      }
      if (auto scheme = context.GetColorScheme()) {
        surfacePathPrefix.AppendLiteral("colorScheme=");
        surfacePathPrefix.AppendInt(int32_t(*scheme));
        surfacePathPrefix.AppendLiteral(" ");
      }
      if (context.GetContextPaint()) {
        const SVGEmbeddingContextPaint* paint = context.GetContextPaint();
        surfacePathPrefix.AppendLiteral("contextPaint=(");
        if (paint->GetFill()) {
          surfacePathPrefix.AppendLiteral(" fill=");
          surfacePathPrefix.AppendInt(paint->GetFill()->ToABGR(), 16);
        }
        if (paint->GetFillOpacity() != 1.0) {
          surfacePathPrefix.AppendLiteral(" fillOpa=");
          surfacePathPrefix.AppendFloat(paint->GetFillOpacity());
        }
        if (paint->GetStroke()) {
          surfacePathPrefix.AppendLiteral(" stroke=");
          surfacePathPrefix.AppendInt(paint->GetStroke()->ToABGR(), 16);
        }
        if (paint->GetStrokeOpacity() != 1.0) {
          surfacePathPrefix.AppendLiteral(" strokeOpa=");
          surfacePathPrefix.AppendFloat(paint->GetStrokeOpacity());
        }
        surfacePathPrefix.AppendLiteral(" ) ");
      }
      surfacePathPrefix.AppendLiteral("]");

      surfacePathPrefix.AppendLiteral(")/");

      ReportValues(aHandleReport, aData, surfacePathPrefix, counter.Values());
    }
  }

  static void ReportTotal(nsIHandleReportCallback* aHandleReport,
                          nsISupports* aData, bool aExplicit,
                          const char* aPathPrefix, const char* aPathInfix,
                          const MemoryTotal& aTotal) {
    nsAutoCString pathPrefix;
    if (aExplicit) {
      pathPrefix.AppendLiteral("explicit/");
    }
    pathPrefix.Append(aPathPrefix);

    nsAutoCString rasterUsedPrefix(pathPrefix);
    rasterUsedPrefix.AppendLiteral("/raster/used/");
    rasterUsedPrefix.Append(aPathInfix);
    ReportValues(aHandleReport, aData, rasterUsedPrefix, aTotal.UsedRaster());

    nsAutoCString rasterUnusedPrefix(pathPrefix);
    rasterUnusedPrefix.AppendLiteral("/raster/unused/");
    rasterUnusedPrefix.Append(aPathInfix);
    ReportValues(aHandleReport, aData, rasterUnusedPrefix,
                 aTotal.UnusedRaster());

    nsAutoCString vectorUsedPrefix(pathPrefix);
    vectorUsedPrefix.AppendLiteral("/vector/used/");
    vectorUsedPrefix.Append(aPathInfix);
    ReportValues(aHandleReport, aData, vectorUsedPrefix, aTotal.UsedVector());

    nsAutoCString vectorUnusedPrefix(pathPrefix);
    vectorUnusedPrefix.AppendLiteral("/vector/unused/");
    vectorUnusedPrefix.Append(aPathInfix);
    ReportValues(aHandleReport, aData, vectorUnusedPrefix,
                 aTotal.UnusedVector());
  }

  static void ReportValues(nsIHandleReportCallback* aHandleReport,
                           nsISupports* aData, const nsACString& aPathPrefix,
                           const MemoryCounter& aCounter) {
    ReportSourceValue(aHandleReport, aData, aPathPrefix, aCounter);

    ReportValue(aHandleReport, aData, KIND_HEAP, aPathPrefix, "decoded-heap",
                "Decoded image data which is stored on the heap.",
                aCounter.DecodedHeap());

    ReportValue(aHandleReport, aData, KIND_NONHEAP, aPathPrefix,
                "decoded-nonheap",
                "Decoded image data which isn't stored on the heap.",
                aCounter.DecodedNonHeap());

    // We don't know for certain whether or not it is on the heap, so let's
    // just report it as non-heap for reporting purposes.
    ReportValue(aHandleReport, aData, KIND_NONHEAP, aPathPrefix,
                "decoded-unknown",
                "Decoded image data which is unknown to be on the heap or not.",
                aCounter.DecodedUnknown());
  }

  static void ReportSourceValue(nsIHandleReportCallback* aHandleReport,
                                nsISupports* aData,
                                const nsACString& aPathPrefix,
                                const MemoryCounter& aCounter) {
    ReportValue(aHandleReport, aData, KIND_HEAP, aPathPrefix, "source",
                "Raster image source data and vector image documents.",
                aCounter.Source());
  }

  static void ReportValue(nsIHandleReportCallback* aHandleReport,
                          nsISupports* aData, int32_t aKind,
                          const nsACString& aPathPrefix,
                          const char* aPathSuffix, const char* aDescription,
                          size_t aValue) {
    if (aValue == 0) {
      return;
    }

    nsAutoCString desc(aDescription);
    nsAutoCString path(aPathPrefix);
    path.Append(aPathSuffix);

    aHandleReport->Callback(""_ns, path, aKind, UNITS_BYTES, aValue, desc,
                            aData);
  }

  static void RecordCounterForRequest(imgRequest* aRequest,
                                      nsTArray<ImageMemoryCounter>* aArray,
                                      bool aIsUsed) {
    SizeOfState state(ImagesMallocSizeOf);
    RefPtr<image::Image> image = aRequest->GetImage();
    if (image) {
      ImageMemoryCounter counter(aRequest, image, state, aIsUsed);
      aArray->AppendElement(std::move(counter));
    } else {
      // We can at least record some information about the image from the
      // request, and mark it as not knowing the image type yet.
      ImageMemoryCounter counter(aRequest, state, aIsUsed);
      aArray->AppendElement(std::move(counter));
    }
  }
};

NS_IMPL_ISUPPORTS(imgMemoryReporter, nsIMemoryReporter)

NS_IMPL_ISUPPORTS(nsProgressNotificationProxy, nsIProgressEventSink,
                  nsIChannelEventSink, nsIInterfaceRequestor)

NS_IMETHODIMP
nsProgressNotificationProxy::OnProgress(nsIRequest* request, int64_t progress,
                                        int64_t progressMax) {
  nsCOMPtr<nsILoadGroup> loadGroup;
  request->GetLoadGroup(getter_AddRefs(loadGroup));

  nsCOMPtr<nsIProgressEventSink> target;
  NS_QueryNotificationCallbacks(mOriginalCallbacks, loadGroup,
                                NS_GET_IID(nsIProgressEventSink),
                                getter_AddRefs(target));
  if (!target) {
    return NS_OK;
  }
  return target->OnProgress(mImageRequest, progress, progressMax);
}

NS_IMETHODIMP
nsProgressNotificationProxy::OnStatus(nsIRequest* request, nsresult status,
                                      const char16_t* statusArg) {
  nsCOMPtr<nsILoadGroup> loadGroup;
  request->GetLoadGroup(getter_AddRefs(loadGroup));

  nsCOMPtr<nsIProgressEventSink> target;
  NS_QueryNotificationCallbacks(mOriginalCallbacks, loadGroup,
                                NS_GET_IID(nsIProgressEventSink),
                                getter_AddRefs(target));
  if (!target) {
    return NS_OK;
  }
  return target->OnStatus(mImageRequest, status, statusArg);
}

NS_IMETHODIMP
nsProgressNotificationProxy::AsyncOnChannelRedirect(
    nsIChannel* oldChannel, nsIChannel* newChannel, uint32_t flags,
    nsIAsyncVerifyRedirectCallback* cb) {
  // Tell the original original callbacks about it too
  nsCOMPtr<nsILoadGroup> loadGroup;
  newChannel->GetLoadGroup(getter_AddRefs(loadGroup));
  nsCOMPtr<nsIChannelEventSink> target;
  NS_QueryNotificationCallbacks(mOriginalCallbacks, loadGroup,
                                NS_GET_IID(nsIChannelEventSink),
                                getter_AddRefs(target));
  if (!target) {
    cb->OnRedirectVerifyCallback(NS_OK);
    return NS_OK;
  }

  // Delegate to |target| if set, reusing |cb|
  return target->AsyncOnChannelRedirect(oldChannel, newChannel, flags, cb);
}

NS_IMETHODIMP
nsProgressNotificationProxy::GetInterface(const nsIID& iid, void** result) {
  if (iid.Equals(NS_GET_IID(nsIProgressEventSink))) {
    *result = static_cast<nsIProgressEventSink*>(this);
    NS_ADDREF_THIS();
    return NS_OK;
  }
  if (iid.Equals(NS_GET_IID(nsIChannelEventSink))) {
    *result = static_cast<nsIChannelEventSink*>(this);
    NS_ADDREF_THIS();
    return NS_OK;
  }
  if (mOriginalCallbacks) {
    return mOriginalCallbacks->GetInterface(iid, result);
  }
  return NS_NOINTERFACE;
}

static void NewRequestAndEntry(bool aForcePrincipalCheckForCacheEntry,
                               imgLoader* aLoader, const ImageCacheKey& aKey,
                               imgRequest** aRequest, imgCacheEntry** aEntry) {
  RefPtr<imgRequest> request = new imgRequest(aLoader, aKey);
  RefPtr<imgCacheEntry> entry =
      new imgCacheEntry(aLoader, request, aForcePrincipalCheckForCacheEntry);
  aLoader->AddToUncachedImages(request);
  request.forget(aRequest);
  entry.forget(aEntry);
}

static bool ShouldRevalidateEntry(imgCacheEntry* aEntry, nsLoadFlags aFlags,
                                  bool aHasExpired) {
  if (aFlags & nsIRequest::LOAD_BYPASS_CACHE) {
    return false;
  }
  if (aFlags & nsIRequest::VALIDATE_ALWAYS) {
    return true;
  }
  if (aEntry->GetMustValidate()) {
    return true;
  }
  if (aHasExpired) {
    // The cache entry has expired...  Determine whether the stale cache
    // entry can be used without validation...
    if (aFlags & (nsIRequest::LOAD_FROM_CACHE | nsIRequest::VALIDATE_NEVER |
                  nsIRequest::VALIDATE_ONCE_PER_SESSION)) {
      // LOAD_FROM_CACHE, VALIDATE_NEVER and VALIDATE_ONCE_PER_SESSION allow
      // stale cache entries to be used unless they have been explicitly marked
      // to indicate that revalidation is necessary.
      return false;
    }
    // Entry is expired, revalidate.
    return true;
  }
  return false;
}

/* Call content policies on cached images that went through a redirect */
static bool ShouldLoadCachedImage(imgRequest* aImgRequest,
                                  Document* aLoadingDocument,
                                  nsIPrincipal* aTriggeringPrincipal,
                                  nsContentPolicyType aPolicyType,
                                  bool aSendCSPViolationReports) {
  /* Call content policies on cached images - Bug 1082837
   * Cached images are keyed off of the first uri in a redirect chain.
   * Hence content policies don't get a chance to test the intermediate hops
   * or the final destination.  Here we test the final destination using
   * mFinalURI off of the imgRequest and passing it into content policies.
   * For Mixed Content Blocker, we do an additional check to determine if any
   * of the intermediary hops went through an insecure redirect with the
   * mHadInsecureRedirect flag
   */
  bool insecureRedirect = aImgRequest->HadInsecureRedirect();
  nsCOMPtr<nsIURI> contentLocation;
  aImgRequest->GetFinalURI(getter_AddRefs(contentLocation));
  nsresult rv;

  nsCOMPtr<nsIPrincipal> loadingPrincipal =
      aLoadingDocument ? aLoadingDocument->NodePrincipal()
                       : aTriggeringPrincipal;
  // If there is no context and also no triggeringPrincipal, then we use a fresh
  // nullPrincipal as the loadingPrincipal because we can not create a loadinfo
  // without a valid loadingPrincipal.
  if (!loadingPrincipal) {
    loadingPrincipal = NullPrincipal::CreateWithoutOriginAttributes();
  }

  Result<RefPtr<LoadInfo>, nsresult> maybeLoadInfo = LoadInfo::Create(
      loadingPrincipal, aTriggeringPrincipal, aLoadingDocument,
      nsILoadInfo::SEC_ONLY_FOR_EXPLICIT_CONTENTSEC_CHECK, aPolicyType);
  if (NS_WARN_IF(maybeLoadInfo.isErr())) {
    return false;
  }
  RefPtr<LoadInfo> secCheckLoadInfo = maybeLoadInfo.unwrap();
  secCheckLoadInfo->SetSendCSPViolationEvents(aSendCSPViolationReports);

  int16_t decision = nsIContentPolicy::REJECT_REQUEST;
  rv = NS_CheckContentLoadPolicy(contentLocation, secCheckLoadInfo, &decision,
                                 nsContentUtils::GetContentPolicy());
  if (NS_FAILED(rv) || !NS_CP_ACCEPTED(decision)) {
    return false;
  }

  // We call all Content Policies above, but we also have to call mcb
  // individually to check the intermediary redirect hops are secure.
  if (insecureRedirect) {
    // Bug 1314356: If the image ended up in the cache upgraded by HSTS and the
    // page uses upgrade-inscure-requests it had an insecure redirect
    // (http->https). We need to invalidate the image and reload it because
    // mixed content blocker only bails if upgrade-insecure-requests is set on
    // the doc and the resource load is http: which would result in an incorrect
    // mixed content warning.
    nsCOMPtr<nsIDocShell> docShell =
        NS_CP_GetDocShellFromContext(ToSupports(aLoadingDocument));
    if (docShell) {
      Document* document = docShell->GetDocument();
      if (document && document->GetUpgradeInsecureRequests(false)) {
        return false;
      }
    }

    if (!aTriggeringPrincipal || !aTriggeringPrincipal->IsSystemPrincipal()) {
      // reset the decision for mixed content blocker check
      decision = nsIContentPolicy::REJECT_REQUEST;
      rv = nsMixedContentBlocker::ShouldLoad(insecureRedirect, contentLocation,
                                             secCheckLoadInfo,
                                             true,  // aReportError
                                             &decision);
      if (NS_FAILED(rv) || !NS_CP_ACCEPTED(decision)) {
        return false;
      }
    }
  }

  return true;
}

// Returns true if this request is compatible with the given CORS mode on the
// given loading principal, and false if the request may not be reused due
// to CORS.
static bool ValidateCORSMode(imgRequest* aRequest, bool aForcePrincipalCheck,
                             CORSMode aCORSMode,
                             nsIPrincipal* aTriggeringPrincipal) {
  // If the entry's CORS mode doesn't match, or the CORS mode matches but the
  // document principal isn't the same, we can't use this request.
  if (aRequest->GetCORSMode() != aCORSMode) {
    return false;
  }

  if (aRequest->GetCORSMode() != CORS_NONE || aForcePrincipalCheck) {
    nsCOMPtr<nsIPrincipal> otherprincipal = aRequest->GetTriggeringPrincipal();

    // If we previously had a principal, but we don't now, we can't use this
    // request.
    if (otherprincipal && !aTriggeringPrincipal) {
      return false;
    }

    if (otherprincipal && aTriggeringPrincipal &&
        !otherprincipal->Equals(aTriggeringPrincipal)) {
      return false;
    }
  }

  return true;
}

static bool ValidateSecurityInfo(imgRequest* aRequest,
                                 bool aForcePrincipalCheck, CORSMode aCORSMode,
                                 nsIPrincipal* aTriggeringPrincipal,
                                 Document* aLoadingDocument,
                                 nsContentPolicyType aPolicyType) {
  if (!ValidateCORSMode(aRequest, aForcePrincipalCheck, aCORSMode,
                        aTriggeringPrincipal)) {
    return false;
  }
  // Content Policy Check on Cached Images
  return ShouldLoadCachedImage(aRequest, aLoadingDocument, aTriggeringPrincipal,
                               aPolicyType,
                               /* aSendCSPViolationReports */ false);
}

static void AdjustPriorityForImages(nsIChannel* aChannel,
                                    nsLoadFlags aLoadFlags,
                                    FetchPriority aFetchPriority,
                                    bool aIsLinkPreload) {
  // Image channels are loaded by default with reduced priority.
  if (nsCOMPtr<nsISupportsPriority> supportsPriority =
          do_QueryInterface(aChannel)) {
    int32_t priority = nsISupportsPriority::PRIORITY_LOW;

    // Adjust priority according to fetchpriorty attribute.
    if (StaticPrefs::network_fetchpriority_enabled()) {
      priority += FETCH_PRIORITY_ADJUSTMENT_FOR(images, aFetchPriority);
    }

    // Further reduce priority for link preload background loads only.
    if (aIsLinkPreload && (aLoadFlags & nsIRequest::LOAD_BACKGROUND)) {
      ++priority;
    }

    supportsPriority->AdjustPriority(priority);
  }

  if (nsCOMPtr<nsIClassOfService> cos = do_QueryInterface(aChannel)) {
    cos->SetFetchPriorityDOM(aFetchPriority);
  }
}

static nsresult NewImageChannel(
    nsIChannel** aResult,
    // If aForcePrincipalCheckForCacheEntry is true, then we will
    // force a principal check even when not using CORS before
    // assuming we have a cache hit on a cache entry that we
    // create for this channel.  This is an out param that should
    // be set to true if this channel ends up depending on
    // aTriggeringPrincipal and false otherwise.
    bool* aForcePrincipalCheckForCacheEntry, nsIURI* aURI,
    nsIURI* aInitialDocumentURI, CORSMode aCORSMode,
    nsIReferrerInfo* aReferrerInfo, nsILoadGroup* aLoadGroup,
    nsLoadFlags aLoadFlags, nsContentPolicyType aPolicyType,
    nsIPrincipal* aTriggeringPrincipal, nsINode* aRequestingNode,
    bool aRespectPrivacy, uint64_t aEarlyHintPreloaderId,
    FetchPriority aFetchPriority, bool aIsLinkPreload) {
  MOZ_ASSERT(aResult);

  nsresult rv;
  nsCOMPtr<nsIHttpChannel> newHttpChannel;

  nsCOMPtr<nsIInterfaceRequestor> callbacks;

  if (aLoadGroup) {
    // Get the notification callbacks from the load group for the new channel.
    //
    // XXX: This is not exactly correct, because the network request could be
    //      referenced by multiple windows...  However, the new channel needs
    //      something.  So, using the 'first' notification callbacks is better
    //      than nothing...
    //
    aLoadGroup->GetNotificationCallbacks(getter_AddRefs(callbacks));
  }

  // Pass in a nullptr loadgroup because this is the underlying network
  // request. This request may be referenced by several proxy image requests
  // (possibly in different documents).
  // If all of the proxy requests are canceled then this request should be
  // canceled too.
  //

  nsSecurityFlags securityFlags =
      nsContentSecurityManager::ComputeSecurityFlags(
          aCORSMode, nsContentSecurityManager::CORSSecurityMapping::
                         CORS_NONE_MAPS_TO_INHERITED_CONTEXT);

  securityFlags |= nsILoadInfo::SEC_ALLOW_CHROME;

  // Note we are calling NS_NewChannelWithTriggeringPrincipal() here with a
  // node and a principal. This is for things like background images that are
  // specified by user stylesheets, where the document is being styled, but
  // the principal is that of the user stylesheet.
  if (aRequestingNode && aTriggeringPrincipal) {
    rv = NS_NewChannelWithTriggeringPrincipal(aResult, aURI, aRequestingNode,
                                              aTriggeringPrincipal,
                                              securityFlags, aPolicyType,
                                              nullptr,  // PerformanceStorage
                                              nullptr,  // loadGroup
                                              callbacks, aLoadFlags);

    if (NS_FAILED(rv)) {
      return rv;
    }

    if (aPolicyType == nsIContentPolicy::TYPE_INTERNAL_IMAGE_FAVICON) {
      // If this is a favicon loading, we will use the originAttributes from the
      // triggeringPrincipal as the channel's originAttributes. This allows the
      // favicon loading from XUL will use the correct originAttributes.

      nsCOMPtr<nsILoadInfo> loadInfo = (*aResult)->LoadInfo();
      rv = loadInfo->SetOriginAttributes(
          aTriggeringPrincipal->OriginAttributesRef());
    }
  } else {
    // either we are loading something inside a document, in which case
    // we should always have a requestingNode, or we are loading something
    // outside a document, in which case the triggeringPrincipal should be the
    // systemPrincipal. However, there are exceptions: one is Notifications
    // which create a channel in the parent process in which case we can't get a
    // requestingNode though we might have a valid triggeringPrincipal.
    rv = NS_NewChannel(aResult, aURI,
                       aTriggeringPrincipal
                           ? aTriggeringPrincipal
                           : nsContentUtils::GetSystemPrincipal(),
                       securityFlags, aPolicyType,
                       nullptr,  // nsICookieJarSettings
                       nullptr,  // PerformanceStorage
                       nullptr,  // loadGroup
                       callbacks, aLoadFlags);

    if (NS_FAILED(rv)) {
      return rv;
    }

    // Use the OriginAttributes from the loading principal, if one is available,
    // and adjust the private browsing ID based on what kind of load the caller
    // has asked us to perform.
    OriginAttributes attrs;
    if (aTriggeringPrincipal) {
      attrs = aTriggeringPrincipal->OriginAttributesRef();
    }
    attrs.mPrivateBrowsingId = aRespectPrivacy ? 1 : 0;

    nsCOMPtr<nsILoadInfo> loadInfo = (*aResult)->LoadInfo();
    rv = loadInfo->SetOriginAttributes(attrs);
  }

  if (NS_FAILED(rv)) {
    return rv;
  }

  // only inherit if we have a principal
  *aForcePrincipalCheckForCacheEntry =
      aTriggeringPrincipal && nsContentUtils::ChannelShouldInheritPrincipal(
                                  aTriggeringPrincipal, aURI,
                                  /* aInheritForAboutBlank */ false,
                                  /* aForceInherit */ false);

  // Initialize HTTP-specific attributes
  newHttpChannel = do_QueryInterface(*aResult);
  if (newHttpChannel) {
    nsCOMPtr<nsIHttpChannelInternal> httpChannelInternal =
        do_QueryInterface(newHttpChannel);
    NS_ENSURE_TRUE(httpChannelInternal, NS_ERROR_UNEXPECTED);
    rv = httpChannelInternal->SetDocumentURI(aInitialDocumentURI);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    if (aReferrerInfo) {
      DebugOnly<nsresult> rv = newHttpChannel->SetReferrerInfo(aReferrerInfo);
      MOZ_ASSERT(NS_SUCCEEDED(rv));
    }

    if (aEarlyHintPreloaderId) {
      rv = httpChannelInternal->SetEarlyHintPreloaderId(aEarlyHintPreloaderId);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  AdjustPriorityForImages(*aResult, aLoadFlags, aFetchPriority, aIsLinkPreload);

  // Create a new loadgroup for this new channel, using the old group as
  // the parent. The indirection keeps the channel insulated from cancels,
  // but does allow a way for this revalidation to be associated with at
  // least one base load group for scheduling/caching purposes.

  nsCOMPtr<nsILoadGroup> loadGroup = do_CreateInstance(NS_LOADGROUP_CONTRACTID);
  nsCOMPtr<nsILoadGroupChild> childLoadGroup = do_QueryInterface(loadGroup);
  if (childLoadGroup) {
    childLoadGroup->SetParentLoadGroup(aLoadGroup);
  }
  (*aResult)->SetLoadGroup(loadGroup);

  return NS_OK;
}

static uint32_t SecondsFromPRTime(PRTime aTime) {
  return nsContentUtils::SecondsFromPRTime(aTime);
}

/* static */
imgCacheEntry::imgCacheEntry(imgLoader* loader, imgRequest* request,
                             bool forcePrincipalCheck)
    : mLoader(loader),
      mRequest(request),
      mDataSize(0),
      mTouchedTime(SecondsFromPRTime(PR_Now())),
      mLoadTime(SecondsFromPRTime(PR_Now())),
      mExpiryTime(CacheExpirationTime::Never()),
      mMustValidate(false),
      // We start off as evicted so we don't try to update the cache.
      // PutIntoCache will set this to false.
      mEvicted(true),
      mHasNoProxies(true),
      mForcePrincipalCheck(forcePrincipalCheck),
      mHasNotified(false) {}

imgCacheEntry::~imgCacheEntry() {
  LOG_FUNC(gImgLog, "imgCacheEntry::~imgCacheEntry()");
}

void imgCacheEntry::Touch(bool updateTime /* = true */) {
  LOG_SCOPE(gImgLog, "imgCacheEntry::Touch");

  if (updateTime) {
    mTouchedTime = SecondsFromPRTime(PR_Now());
  }

  UpdateCache();
}

void imgCacheEntry::UpdateCache(int32_t diff /* = 0 */) {
  // Don't update the cache if we've been removed from it or it doesn't care
  // about our size or usage.
  if (!Evicted() && HasNoProxies()) {
    mLoader->CacheEntriesChanged(diff);
  }
}

void imgCacheEntry::UpdateLoadTime() {
  mLoadTime = SecondsFromPRTime(PR_Now());
}

void imgCacheEntry::SetHasNoProxies(bool hasNoProxies) {
  if (MOZ_LOG_TEST(gImgLog, LogLevel::Debug)) {
    if (hasNoProxies) {
      LOG_FUNC_WITH_PARAM(gImgLog, "imgCacheEntry::SetHasNoProxies true", "uri",
                          mRequest->CacheKey().URI());
    } else {
      LOG_FUNC_WITH_PARAM(gImgLog, "imgCacheEntry::SetHasNoProxies false",
                          "uri", mRequest->CacheKey().URI());
    }
  }

  mHasNoProxies = hasNoProxies;
}

imgCacheQueue::imgCacheQueue() : mDirty(false), mSize(0) {}

void imgCacheQueue::UpdateSize(int32_t diff) { mSize += diff; }

uint32_t imgCacheQueue::GetSize() const { return mSize; }

void imgCacheQueue::Remove(imgCacheEntry* entry) {
  uint64_t index = mQueue.IndexOf(entry);
  if (index == queueContainer::NoIndex) {
    return;
  }

  mSize -= mQueue[index]->GetDataSize();

  // If the queue is clean and this is the first entry,
  // then we can efficiently remove the entry without
  // dirtying the sort order.
  if (!IsDirty() && index == 0) {
    std::pop_heap(mQueue.begin(), mQueue.end(), imgLoader::CompareCacheEntries);
    mQueue.RemoveLastElement();
    return;
  }

  // Remove from the middle of the list.  This potentially
  // breaks the binary heap sort order.
  mQueue.RemoveElementAt(index);

  // If we only have one entry or the queue is empty, though,
  // then the sort order is still effectively good.  Simply
  // refresh the list to clear the dirty flag.
  if (mQueue.Length() <= 1) {
    Refresh();
    return;
  }

  // Otherwise we must mark the queue dirty and potentially
  // trigger an expensive sort later.
  MarkDirty();
}

void imgCacheQueue::Push(imgCacheEntry* entry) {
  mSize += entry->GetDataSize();

  RefPtr<imgCacheEntry> refptr(entry);
  mQueue.AppendElement(std::move(refptr));
  // If we're not dirty already, then we can efficiently add this to the
  // binary heap immediately.  This is only O(log n).
  if (!IsDirty()) {
    std::push_heap(mQueue.begin(), mQueue.end(),
                   imgLoader::CompareCacheEntries);
  }
}

already_AddRefed<imgCacheEntry> imgCacheQueue::Pop() {
  if (mQueue.IsEmpty()) {
    return nullptr;
  }
  if (IsDirty()) {
    Refresh();
  }

  std::pop_heap(mQueue.begin(), mQueue.end(), imgLoader::CompareCacheEntries);
  RefPtr<imgCacheEntry> entry = mQueue.PopLastElement();

  mSize -= entry->GetDataSize();
  return entry.forget();
}

void imgCacheQueue::Refresh() {
  // Resort the list.  This is an O(3 * n) operation and best avoided
  // if possible.
  std::make_heap(mQueue.begin(), mQueue.end(), imgLoader::CompareCacheEntries);
  mDirty = false;
}

void imgCacheQueue::MarkDirty() { mDirty = true; }

bool imgCacheQueue::IsDirty() { return mDirty; }

uint32_t imgCacheQueue::GetNumElements() const { return mQueue.Length(); }

bool imgCacheQueue::Contains(imgCacheEntry* aEntry) const {
  return mQueue.Contains(aEntry);
}

imgCacheQueue::iterator imgCacheQueue::begin() { return mQueue.begin(); }

imgCacheQueue::const_iterator imgCacheQueue::begin() const {
  return mQueue.begin();
}

imgCacheQueue::iterator imgCacheQueue::end() { return mQueue.end(); }

imgCacheQueue::const_iterator imgCacheQueue::end() const {
  return mQueue.end();
}

nsresult imgLoader::CreateNewProxyForRequest(
    imgRequest* aRequest, nsIURI* aURI, nsILoadGroup* aLoadGroup,
    Document* aLoadingDocument, imgINotificationObserver* aObserver,
    nsLoadFlags aLoadFlags, imgRequestProxy** _retval) {
  LOG_SCOPE_WITH_PARAM(gImgLog, "imgLoader::CreateNewProxyForRequest",
                       "imgRequest", aRequest);

  /* XXX If we move decoding onto separate threads, we should save off the
     calling thread here and pass it off to |proxyRequest| so that it call
     proxy calls to |aObserver|.
   */

  RefPtr<imgRequestProxy> proxyRequest = new imgRequestProxy();

  /* It is important to call |SetLoadFlags()| before calling |Init()| because
     |Init()| adds the request to the loadgroup.
   */
  proxyRequest->SetLoadFlags(aLoadFlags);

  // init adds itself to imgRequest's list of observers
  nsresult rv = proxyRequest->Init(aRequest, aLoadGroup, aURI, aObserver);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  proxyRequest.forget(_retval);
  return NS_OK;
}

class imgCacheExpirationTracker final
    : public nsExpirationTracker<imgCacheEntry, 3> {
  enum { TIMEOUT_SECONDS = 10 };

 public:
  imgCacheExpirationTracker();

 protected:
  void NotifyExpired(imgCacheEntry* entry) override;
};

imgCacheExpirationTracker::imgCacheExpirationTracker()
    : nsExpirationTracker<imgCacheEntry, 3>(TIMEOUT_SECONDS * 1000,
                                            "imgCacheExpirationTracker"_ns) {}

void imgCacheExpirationTracker::NotifyExpired(imgCacheEntry* entry) {
  // Hold on to a reference to this entry, because the expiration tracker
  // mechanism doesn't.
  RefPtr<imgCacheEntry> kungFuDeathGrip(entry);

  if (MOZ_LOG_TEST(gImgLog, LogLevel::Debug)) {
    RefPtr<imgRequest> req = entry->GetRequest();
    if (req) {
      LOG_FUNC_WITH_PARAM(gImgLog, "imgCacheExpirationTracker::NotifyExpired",
                          "entry", req->CacheKey().URI());
    }
  }

  // We can be called multiple times on the same entry. Don't do work multiple
  // times.
  if (!entry->Evicted()) {
    entry->Loader()->RemoveFromCache(entry);
  }

  entry->Loader()->VerifyCacheSizes();
}

///////////////////////////////////////////////////////////////////////////////
// imgLoader
///////////////////////////////////////////////////////////////////////////////

double imgLoader::sCacheTimeWeight;
uint32_t imgLoader::sCacheMaxSize;
imgMemoryReporter* imgLoader::sMemReporter;

NS_IMPL_ISUPPORTS(imgLoader, imgILoader, nsIContentSniffer, imgICache,
                  nsISupportsWeakReference, nsIObserver)

static imgLoader* gNormalLoader = nullptr;
static imgLoader* gPrivateBrowsingLoader = nullptr;

/* static */
already_AddRefed<imgLoader> imgLoader::CreateImageLoader() {
  // In some cases, such as xpctests, XPCOM modules are not automatically
  // initialized.  We need to make sure that our module is initialized before
  // we hand out imgLoader instances and code starts using them.
  mozilla::image::EnsureModuleInitialized();

  RefPtr<imgLoader> loader = new imgLoader();
  loader->Init();

  return loader.forget();
}

imgLoader* imgLoader::NormalLoader() {
  if (!gNormalLoader) {
    gNormalLoader = CreateImageLoader().take();
  }
  return gNormalLoader;
}

imgLoader* imgLoader::PrivateBrowsingLoader() {
  if (!gPrivateBrowsingLoader) {
    gPrivateBrowsingLoader = CreateImageLoader().take();
    gPrivateBrowsingLoader->RespectPrivacyNotifications();
  }
  return gPrivateBrowsingLoader;
}

imgLoader::imgLoader()
    : mUncachedImagesMutex("imgLoader::UncachedImages"),
      mRespectPrivacy(false) {
  sMemReporter->AddRef();
  sMemReporter->RegisterLoader(this);
}

imgLoader::~imgLoader() {
  ClearImageCache();
  {
    // If there are any of our imgRequest's left they are in the uncached
    // images set, so clear their pointer to us.
    MutexAutoLock lock(mUncachedImagesMutex);
    for (RefPtr<imgRequest> req : mUncachedImages) {
      req->ClearLoader();
    }
  }
  sMemReporter->UnregisterLoader(this);
  sMemReporter->Release();
}

void imgLoader::VerifyCacheSizes() {
#ifdef DEBUG
  if (!mCacheTracker) {
    return;
  }

  uint32_t cachesize = mCache.Count();
  uint32_t queuesize = mCacheQueue.GetNumElements();
  uint32_t trackersize = 0;
  for (nsExpirationTracker<imgCacheEntry, 3>::Iterator it(mCacheTracker.get());
       it.Next();) {
    trackersize++;
  }
  MOZ_ASSERT(queuesize == trackersize, "Queue and tracker sizes out of sync!");
  MOZ_ASSERT(queuesize <= cachesize, "Queue has more elements than cache!");
#endif
}

void imgLoader::GlobalInit() {
  sCacheTimeWeight = StaticPrefs::image_cache_timeweight_AtStartup() / 1000.0;
  int32_t cachesize = StaticPrefs::image_cache_size_AtStartup();
  sCacheMaxSize = cachesize > 0 ? cachesize : 0;

  sMemReporter = new imgMemoryReporter();
  RegisterStrongAsyncMemoryReporter(do_AddRef(sMemReporter));
  RegisterImagesContentUsedUncompressedDistinguishedAmount(
      imgMemoryReporter::ImagesContentUsedUncompressedDistinguishedAmount);
}

void imgLoader::ShutdownMemoryReporter() {
  UnregisterImagesContentUsedUncompressedDistinguishedAmount();
  UnregisterStrongMemoryReporter(sMemReporter);
}

nsresult imgLoader::InitCache() {
  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (!os) {
    return NS_ERROR_FAILURE;
  }

  os->AddObserver(this, "memory-pressure", false);
  os->AddObserver(this, "chrome-flush-caches", false);
  os->AddObserver(this, "last-pb-context-exited", false);
  os->AddObserver(this, "profile-before-change", false);
  os->AddObserver(this, "xpcom-shutdown", false);

  mCacheTracker = MakeUnique<imgCacheExpirationTracker>();

  return NS_OK;
}

nsresult imgLoader::Init() {
  InitCache();

  return NS_OK;
}

NS_IMETHODIMP
imgLoader::RespectPrivacyNotifications() {
  mRespectPrivacy = true;
  return NS_OK;
}

NS_IMETHODIMP
imgLoader::Observe(nsISupports* aSubject, const char* aTopic,
                   const char16_t* aData) {
  if (strcmp(aTopic, "memory-pressure") == 0) {
    MinimizeCache();
  } else if (strcmp(aTopic, "chrome-flush-caches") == 0) {
    MinimizeCache();
    ClearImageCache({ClearOption::ChromeOnly});
  } else if (strcmp(aTopic, "last-pb-context-exited") == 0) {
    if (mRespectPrivacy) {
      ClearImageCache();
    }
  } else if (strcmp(aTopic, "profile-before-change") == 0) {
    mCacheTracker = nullptr;
  } else if (strcmp(aTopic, "xpcom-shutdown") == 0) {
    mCacheTracker = nullptr;
    ShutdownMemoryReporter();

  } else {
    // (Nothing else should bring us here)
    MOZ_ASSERT(0, "Invalid topic received");
  }

  return NS_OK;
}

NS_IMETHODIMP
imgLoader::ClearCache(JS::Handle<JS::Value> aChrome) {
  nsresult rv = NS_OK;

  Maybe<bool> chrome =
      aChrome.isBoolean() ? Some(aChrome.toBoolean()) : Nothing();
  if (XRE_IsParentProcess()) {
    bool privateLoader = this == gPrivateBrowsingLoader;
    rv = ClearCache(Some(privateLoader), chrome, Nothing(), Nothing(),
                    Nothing());

    if (this == gNormalLoader || this == gPrivateBrowsingLoader) {
      return rv;
    }

    // NOTE: There can be other loaders created with
    //       Cc["@mozilla.org/image/loader;1"].createInstance(Ci.imgILoader).
    //       If ClearCache is called on them, the above static ClearCache
    //       doesn't handle it, and ClearImageCache needs to be called on
    //       the current instance.
    //       The favicon handling and some tests can create such loaders.
  }

  ClearOptions options;
  if (chrome) {
    if (*chrome) {
      options += ClearOption::ChromeOnly;
    } else {
      options += ClearOption::ContentOnly;
    }
  }
  nsresult rv2 = ClearImageCache(options);

  if (NS_FAILED(rv)) {
    return rv;
  }
  return rv2;
}

/*static */
nsresult imgLoader::ClearCache(
    mozilla::Maybe<bool> aPrivateLoader /* = mozilla::Nothing() */,
    mozilla::Maybe<bool> aChrome /* = mozilla::Nothing() */,
    const mozilla::Maybe<nsCOMPtr<nsIPrincipal>>&
        aPrincipal /* = mozilla::Nothing() */,
    const mozilla::Maybe<nsCString>& aSchemelessSite /* = mozilla::Nothing() */,
    const mozilla::Maybe<mozilla::OriginAttributesPattern>&
        aPattern /* = mozilla::Nothing() */,
    const mozilla::Maybe<nsCString>& aURL /* = mozilla::Nothing() */) {
  if (XRE_IsParentProcess()) {
    for (auto* cp : ContentParent::AllProcesses(ContentParent::eLive)) {
      (void)cp->SendClearImageCache(aPrivateLoader, aChrome, aPrincipal,
                                    aSchemelessSite, aPattern, aURL);
    }
  }

  if (aPrincipal) {
    imgLoader* loader;
    if ((*aPrincipal)->OriginAttributesRef().IsPrivateBrowsing()) {
      loader = imgLoader::PrivateBrowsingLoader();
    } else {
      loader = imgLoader::NormalLoader();
    }

    loader->RemoveEntriesInternal(aPrincipal, Nothing(), Nothing(), Nothing());
    return NS_OK;
  }

  if (aSchemelessSite) {
    if (!aPrivateLoader || !*aPrivateLoader) {
      nsresult rv = imgLoader::NormalLoader()->RemoveEntriesInternal(
          Nothing(), aSchemelessSite, aPattern, Nothing());
      NS_ENSURE_SUCCESS(rv, rv);
    }
    if (!aPrivateLoader || *aPrivateLoader) {
      nsresult rv = imgLoader::PrivateBrowsingLoader()->RemoveEntriesInternal(
          Nothing(), aSchemelessSite, aPattern, Nothing());
      NS_ENSURE_SUCCESS(rv, rv);
    }
    return NS_OK;
  }

  if (aURL) {
    nsCOMPtr<nsIURI> uri;
    nsresult rv = NS_NewURI(getter_AddRefs(uri), *aURL);
    NS_ENSURE_SUCCESS(rv, rv);

    if (!aPrivateLoader || !*aPrivateLoader) {
      nsresult rv = imgLoader::NormalLoader()->RemoveEntry(uri, nullptr);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    if (!aPrivateLoader || *aPrivateLoader) {
      nsresult rv =
          imgLoader::PrivateBrowsingLoader()->RemoveEntry(uri, nullptr);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    return NS_OK;
  }

  ClearOptions options;
  if (aChrome) {
    if (*aChrome) {
      options += ClearOption::ChromeOnly;
    } else {
      options += ClearOption::ContentOnly;
    }
  }

  if (!aPrivateLoader || !*aPrivateLoader) {
    nsresult rv = imgLoader::NormalLoader()->ClearImageCache(options);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  if (!aPrivateLoader || *aPrivateLoader) {
    nsresult rv = imgLoader::PrivateBrowsingLoader()->ClearImageCache(options);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

NS_IMETHODIMP
imgLoader::RemoveEntriesFromPrincipalInAllProcesses(nsIPrincipal* aPrincipal) {
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsCOMPtr<nsIPrincipal> principal = aPrincipal;
  return ClearCache(Nothing(), Nothing(), Some(principal));
}

NS_IMETHODIMP
imgLoader::RemoveEntriesFromSiteInAllProcesses(
    const nsACString& aSchemelessSite,
    JS::Handle<JS::Value> aOriginAttributesPattern, JSContext* aCx) {
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  OriginAttributesPattern pattern;
  if (!aOriginAttributesPattern.isObject() ||
      !pattern.Init(aCx, aOriginAttributesPattern)) {
    return NS_ERROR_INVALID_ARG;
  }

  return ClearCache(Nothing(), Nothing(), Nothing(),
                    Some(nsCString(aSchemelessSite)), Some(pattern));
}

nsresult imgLoader::RemoveEntriesInternal(
    const Maybe<nsCOMPtr<nsIPrincipal>>& aPrincipal,
    const Maybe<nsCString>& aSchemelessSite,
    const Maybe<OriginAttributesPattern>& aPattern,
    const mozilla::Maybe<nsCString>& aURL) {
  // Can only clear by either principal or site + pattern.
  if ((!aPrincipal && !aSchemelessSite && !aURL) ||
      (aPrincipal && aSchemelessSite) || (aPrincipal && aURL) ||
      (aSchemelessSite && aURL) ||
      aSchemelessSite.isSome() != aPattern.isSome()) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoTArray<RefPtr<imgCacheEntry>, 128> entriesToBeRemoved;

  // For base domain we only clear the non-chrome cache.
  for (const auto& entry : mCache) {
    const auto& key = entry.GetKey();

    if (SharedSubResourceCacheUtils::ShouldClearEntry(
            key.URI(), key.PartitionPrincipal(), Nothing(), aPrincipal,
            aSchemelessSite, aPattern, aURL)) {
      entriesToBeRemoved.AppendElement(entry.GetData());
    }
  }

  for (auto& entry : entriesToBeRemoved) {
    if (!RemoveFromCache(entry)) {
      NS_WARNING(
          "Couldn't remove an entry from the cache in "
          "RemoveEntriesInternal()\n");
    }
  }

  return NS_OK;
}

constexpr auto AllCORSModes() {
  return MakeInclusiveEnumeratedRange(kFirstCORSMode, kLastCORSMode);
}

NS_IMETHODIMP
imgLoader::RemoveEntry(nsIURI* aURI, Document* aDoc) {
  if (!aURI) {
    return NS_OK;
  }
  for (auto corsMode : AllCORSModes()) {
    ImageCacheKey key(aURI, corsMode, aDoc);
    RemoveFromCache(key);
  }
  return NS_OK;
}

NS_IMETHODIMP
imgLoader::FindEntryProperties(nsIURI* uri, Document* aDoc,
                               nsIProperties** _retval) {
  *_retval = nullptr;

  for (auto corsMode : AllCORSModes()) {
    ImageCacheKey key(uri, corsMode, aDoc);
    RefPtr<imgCacheEntry> entry;
    if (!mCache.Get(key, getter_AddRefs(entry)) || !entry) {
      continue;
    }
    if (mCacheTracker && entry->HasNoProxies()) {
      mCacheTracker->MarkUsed(entry);
    }
    RefPtr<imgRequest> request = entry->GetRequest();
    if (request) {
      nsCOMPtr<nsIProperties> properties = request->Properties();
      properties.forget(_retval);
      return NS_OK;
    }
  }
  return NS_OK;
}

NS_IMETHODIMP_(void)
imgLoader::ClearCacheForControlledDocument(Document* aDoc) {
  MOZ_ASSERT(aDoc);
  AutoTArray<RefPtr<imgCacheEntry>, 128> entriesToBeRemoved;
  for (const auto& entry : mCache) {
    const auto& key = entry.GetKey();
    if (key.ControlledDocument() == aDoc) {
      entriesToBeRemoved.AppendElement(entry.GetData());
    }
  }
  for (auto& entry : entriesToBeRemoved) {
    if (!RemoveFromCache(entry)) {
      NS_WARNING(
          "Couldn't remove an entry from the cache in "
          "ClearCacheForControlledDocument()\n");
    }
  }
}

void imgLoader::Shutdown() {
  NS_IF_RELEASE(gNormalLoader);
  gNormalLoader = nullptr;
  NS_IF_RELEASE(gPrivateBrowsingLoader);
  gPrivateBrowsingLoader = nullptr;
}

bool imgLoader::PutIntoCache(const ImageCacheKey& aKey, imgCacheEntry* entry) {
  LOG_STATIC_FUNC_WITH_PARAM(gImgLog, "imgLoader::PutIntoCache", "uri",
                             aKey.URI());

  // Check to see if this request already exists in the cache. If so, we'll
  // replace the old version.
  RefPtr<imgCacheEntry> tmpCacheEntry;
  if (mCache.Get(aKey, getter_AddRefs(tmpCacheEntry)) && tmpCacheEntry) {
    MOZ_LOG(
        gImgLog, LogLevel::Debug,
        ("[this=%p] imgLoader::PutIntoCache -- Element already in the cache",
         nullptr));
    RefPtr<imgRequest> tmpRequest = tmpCacheEntry->GetRequest();

    // If it already exists, and we're putting the same key into the cache, we
    // should remove the old version.
    MOZ_LOG(gImgLog, LogLevel::Debug,
            ("[this=%p] imgLoader::PutIntoCache -- Replacing cached element",
             nullptr));

    RemoveFromCache(aKey);
  } else {
    MOZ_LOG(gImgLog, LogLevel::Debug,
            ("[this=%p] imgLoader::PutIntoCache --"
             " Element NOT already in the cache",
             nullptr));
  }

  mCache.InsertOrUpdate(aKey, RefPtr{entry});

  // We can be called to resurrect an evicted entry.
  if (entry->Evicted()) {
    entry->SetEvicted(false);
  }

  // If we're resurrecting an entry with no proxies, put it back in the
  // tracker and queue.
  if (entry->HasNoProxies()) {
    nsresult addrv = NS_OK;

    if (mCacheTracker) {
      addrv = mCacheTracker->AddObject(entry);
    }

    if (NS_SUCCEEDED(addrv)) {
      mCacheQueue.Push(entry);
    }
  }

  RefPtr<imgRequest> request = entry->GetRequest();
  request->SetIsInCache(true);
  RemoveFromUncachedImages(request);

  return true;
}

bool imgLoader::SetHasNoProxies(imgRequest* aRequest, imgCacheEntry* aEntry) {
  LOG_STATIC_FUNC_WITH_PARAM(gImgLog, "imgLoader::SetHasNoProxies", "uri",
                             aRequest->CacheKey().URI());

  aEntry->SetHasNoProxies(true);

  if (aEntry->Evicted()) {
    return false;
  }

  nsresult addrv = NS_OK;

  if (mCacheTracker) {
    addrv = mCacheTracker->AddObject(aEntry);
  }

  if (NS_SUCCEEDED(addrv)) {
    mCacheQueue.Push(aEntry);
  }

  CheckCacheLimits();

  return true;
}

bool imgLoader::SetHasProxies(imgRequest* aRequest) {
  VerifyCacheSizes();

  const ImageCacheKey& key = aRequest->CacheKey();

  LOG_STATIC_FUNC_WITH_PARAM(gImgLog, "imgLoader::SetHasProxies", "uri",
                             key.URI());

  RefPtr<imgCacheEntry> entry;
  if (mCache.Get(key, getter_AddRefs(entry)) && entry) {
    // Make sure the cache entry is for the right request
    RefPtr<imgRequest> entryRequest = entry->GetRequest();
    if (entryRequest == aRequest && entry->HasNoProxies()) {
      mCacheQueue.Remove(entry);

      if (mCacheTracker) {
        mCacheTracker->RemoveObject(entry);
      }

      entry->SetHasNoProxies(false);

      return true;
    }
  }

  return false;
}

void imgLoader::CacheEntriesChanged(int32_t aSizeDiff /* = 0 */) {
  // We only need to dirty the queue if there is any sorting
  // taking place.  Empty or single-entry lists can't become
  // dirty.
  if (mCacheQueue.GetNumElements() > 1) {
    mCacheQueue.MarkDirty();
  }
  mCacheQueue.UpdateSize(aSizeDiff);
}

void imgLoader::CheckCacheLimits() {
  if (mCacheQueue.GetNumElements() == 0) {
    NS_ASSERTION(mCacheQueue.GetSize() == 0,
                 "imgLoader::CheckCacheLimits -- incorrect cache size");
  }

  // Remove entries from the cache until we're back at our desired max size.
  while (mCacheQueue.GetSize() > sCacheMaxSize) {
    // Remove the first entry in the queue.
    RefPtr<imgCacheEntry> entry(mCacheQueue.Pop());

    NS_ASSERTION(entry, "imgLoader::CheckCacheLimits -- NULL entry pointer");

    if (MOZ_LOG_TEST(gImgLog, LogLevel::Debug)) {
      RefPtr<imgRequest> req = entry->GetRequest();
      if (req) {
        LOG_STATIC_FUNC_WITH_PARAM(gImgLog, "imgLoader::CheckCacheLimits",
                                   "entry", req->CacheKey().URI());
      }
    }

    if (entry) {
      // We just popped this entry from the queue, so pass AlreadyRemoved
      // to avoid searching the queue again in RemoveFromCache.
      RemoveFromCache(entry, QueueState::AlreadyRemoved);
    }
  }
}

bool imgLoader::ValidateRequestWithNewChannel(
    imgRequest* request, nsIURI* aURI, nsIURI* aInitialDocumentURI,
    nsIReferrerInfo* aReferrerInfo, nsILoadGroup* aLoadGroup,
    imgINotificationObserver* aObserver, Document* aLoadingDocument,
    uint64_t aInnerWindowId, nsLoadFlags aLoadFlags,
    nsContentPolicyType aLoadPolicyType, imgRequestProxy** aProxyRequest,
    nsIPrincipal* aTriggeringPrincipal, CORSMode aCORSMode, bool aLinkPreload,
    uint64_t aEarlyHintPreloaderId, FetchPriority aFetchPriority,
    bool* aNewChannelCreated) {
  // now we need to insert a new channel request object in between the real
  // request and the proxy that basically delays loading the image until it
  // gets a 304 or figures out that this needs to be a new request

  nsresult rv;

  // If we're currently in the middle of validating this request, just hand
  // back a proxy to it; the required work will be done for us.
  if (imgCacheValidator* validator = request->GetValidator()) {
    rv = CreateNewProxyForRequest(request, aURI, aLoadGroup, aLoadingDocument,
                                  aObserver, aLoadFlags, aProxyRequest);
    if (NS_FAILED(rv)) {
      return false;
    }

    if (*aProxyRequest) {
      imgRequestProxy* proxy = static_cast<imgRequestProxy*>(*aProxyRequest);

      // We will send notifications from imgCacheValidator::OnStartRequest().
      // In the mean time, we must defer notifications because we are added to
      // the imgRequest's proxy list, and we can get extra notifications
      // resulting from methods such as StartDecoding(). See bug 579122.
      proxy->MarkValidating();

      if (aLinkPreload) {
        MOZ_ASSERT(aLoadingDocument);
        auto preloadKey = PreloadHashKey::CreateAsImage(
            aURI, aTriggeringPrincipal, aCORSMode);
        proxy->NotifyOpen(preloadKey, aLoadingDocument, true);
      }

      // Attach the proxy without notifying
      validator->AddProxy(proxy);
    }

    return true;
  }
  // We will rely on Necko to cache this request when it's possible, and to
  // tell imgCacheValidator::OnStartRequest whether the request came from its
  // cache.
  nsCOMPtr<nsIChannel> newChannel;
  bool forcePrincipalCheck;
  rv = NewImageChannel(getter_AddRefs(newChannel), &forcePrincipalCheck, aURI,
                       aInitialDocumentURI, aCORSMode, aReferrerInfo,
                       aLoadGroup, aLoadFlags, aLoadPolicyType,
                       aTriggeringPrincipal, aLoadingDocument, mRespectPrivacy,
                       aEarlyHintPreloaderId, aFetchPriority, aLinkPreload);
  if (NS_FAILED(rv)) {
    return false;
  }

  if (aNewChannelCreated) {
    *aNewChannelCreated = true;
  }

  RefPtr<imgRequestProxy> req;
  rv = CreateNewProxyForRequest(request, aURI, aLoadGroup, aLoadingDocument,
                                aObserver, aLoadFlags, getter_AddRefs(req));
  if (NS_FAILED(rv)) {
    return false;
  }

  // Make sure that OnStatus/OnProgress calls have the right request set...
  RefPtr<nsProgressNotificationProxy> progressproxy =
      new nsProgressNotificationProxy(newChannel, req);
  if (!progressproxy) {
    return false;
  }

  RefPtr<imgCacheValidator> hvc =
      new imgCacheValidator(progressproxy, this, request, aLoadingDocument,
                            aInnerWindowId, forcePrincipalCheck);

  // Casting needed here to get past multiple inheritance.
  nsCOMPtr<nsIStreamListener> listener =
      static_cast<nsIThreadRetargetableStreamListener*>(hvc);
  NS_ENSURE_TRUE(listener, false);

  // We must set the notification callbacks before setting up the
  // CORS listener, because that's also interested inthe
  // notification callbacks.
  newChannel->SetNotificationCallbacks(hvc);

  request->SetValidator(hvc);

  // We will send notifications from imgCacheValidator::OnStartRequest().
  // In the mean time, we must defer notifications because we are added to
  // the imgRequest's proxy list, and we can get extra notifications
  // resulting from methods such as StartDecoding(). See bug 579122.
  req->MarkValidating();

  if (aLinkPreload) {
    MOZ_ASSERT(aLoadingDocument);
    auto preloadKey =
        PreloadHashKey::CreateAsImage(aURI, aTriggeringPrincipal, aCORSMode);
    req->NotifyOpen(preloadKey, aLoadingDocument, true);
  }

  // Add the proxy without notifying
  hvc->AddProxy(req);

  rv = newChannel->AsyncOpen(listener);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    req->CancelAndForgetObserver(rv);
    // This will notify any current or future <link preload> tags.  Pass the
    // non-open channel so that we can read loadinfo and referrer info of that
    // channel.
    req->NotifyStart(newChannel);
    // Use the non-channel overload of this method to force the notification to
    // happen.  The preload request has not been assigned a channel.
    req->NotifyStop(rv);
    return false;
  }

  req.forget(aProxyRequest);
  return true;
}

void imgLoader::NotifyObserversForCachedImage(
    imgCacheEntry* aEntry, imgRequest* request, nsIURI* aURI,
    nsIReferrerInfo* aReferrerInfo, Document* aLoadingDocument,
    nsIPrincipal* aTriggeringPrincipal, CORSMode aCORSMode,
    uint64_t aEarlyHintPreloaderId, FetchPriority aFetchPriority) {
  if (aEntry->HasNotified()) {
    return;
  }

  nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();

  if (!obsService->HasObservers("http-on-resource-cache-response")) {
    return;
  }

  aEntry->SetHasNotified();

  nsCOMPtr<nsIChannel> newChannel;
  bool forcePrincipalCheck;
  nsresult rv =
      NewImageChannel(getter_AddRefs(newChannel), &forcePrincipalCheck, aURI,
                      nullptr, aCORSMode, aReferrerInfo, nullptr, 0,
                      nsIContentPolicy::TYPE_INTERNAL_IMAGE,
                      aTriggeringPrincipal, aLoadingDocument, mRespectPrivacy,
                      aEarlyHintPreloaderId, aFetchPriority, false);
  if (NS_FAILED(rv)) {
    return;
  }

  RefPtr<HttpBaseChannel> httpBaseChannel = do_QueryObject(newChannel);
  if (httpBaseChannel) {
    httpBaseChannel->SetDummyChannelForCachedResource();
    newChannel->SetContentType(nsDependentCString(request->GetMimeType()));
    RefPtr<mozilla::image::Image> image = request->GetImage();
    if (image) {
      newChannel->SetContentLength(request->GetContentLength());
    }
    obsService->NotifyObservers(newChannel, "http-on-resource-cache-response",
                                nullptr);
  }
}

bool imgLoader::ValidateEntry(
    imgCacheEntry* aEntry, nsIURI* aURI, nsIURI* aInitialDocumentURI,
    nsIReferrerInfo* aReferrerInfo, nsILoadGroup* aLoadGroup,
    imgINotificationObserver* aObserver, Document* aLoadingDocument,
    nsLoadFlags aLoadFlags, nsContentPolicyType aLoadPolicyType,
    bool aCanMakeNewChannel, bool* aNewChannelCreated,
    imgRequestProxy** aProxyRequest, nsIPrincipal* aTriggeringPrincipal,
    CORSMode aCORSMode, bool aLinkPreload, uint64_t aEarlyHintPreloaderId,
    FetchPriority aFetchPriority) {
  LOG_SCOPE(gImgLog, "imgLoader::ValidateEntry");

  // If the expiration time is zero, then the request has not gotten far enough
  // to know when it will expire, or we know it will never expire (see
  // nsContentUtils::GetSubresourceCacheValidationInfo).
  bool hasExpired = aEntry->GetExpiryTime().IsExpired();

  // Special treatment for file URLs - aEntry has expired if file has changed
  if (nsCOMPtr<nsIFileURL> fileUrl = do_QueryInterface(aURI)) {
    uint32_t lastModTime = aEntry->GetLoadTime();
    nsCOMPtr<nsIFile> theFile;
    if (NS_SUCCEEDED(fileUrl->GetFile(getter_AddRefs(theFile)))) {
      PRTime fileLastMod;
      if (NS_SUCCEEDED(theFile->GetLastModifiedTime(&fileLastMod))) {
        // nsIFile uses millisec, NSPR usec.
        fileLastMod *= 1000;
        hasExpired = SecondsFromPRTime((PRTime)fileLastMod) > lastModTime;
      }
    }
  }

  RefPtr<imgRequest> request(aEntry->GetRequest());

  if (!request) {
    return false;
  }

  if (!ValidateSecurityInfo(request, aEntry->ForcePrincipalCheck(), aCORSMode,
                            aTriggeringPrincipal, aLoadingDocument,
                            aLoadPolicyType)) {
    return false;
  }

  // data URIs are immutable and by their nature can't leak data, so we can
  // just return true in that case.  Doing so would mean that shift-reload
  // doesn't reload data URI documents/images though (which is handy for
  // debugging during gecko development) so we make an exception in that case.
  if (aURI->SchemeIs("data") && !(aLoadFlags & nsIRequest::LOAD_BYPASS_CACHE)) {
    return true;
  }

  bool validateRequest = false;

  if (!request->CanReuseWithoutValidation(aLoadingDocument)) {
    // If we would need to revalidate this entry, but we're being told to
    // bypass the cache, we don't allow this entry to be used.
    if (aLoadFlags & nsIRequest::LOAD_BYPASS_CACHE) {
      return false;
    }

    if (MOZ_UNLIKELY(ChaosMode::isActive(ChaosFeature::ImageCache))) {
      if (ChaosMode::randomUint32LessThan(4) < 1) {
        return false;
      }
    }

    // Determine whether the cache aEntry must be revalidated...
    validateRequest = ShouldRevalidateEntry(aEntry, aLoadFlags, hasExpired);

    MOZ_LOG(gImgLog, LogLevel::Debug,
            ("imgLoader::ValidateEntry validating cache entry. "
             "validateRequest = %d",
             validateRequest));
  } else if (!aLoadingDocument && MOZ_LOG_TEST(gImgLog, LogLevel::Debug)) {
    MOZ_LOG(gImgLog, LogLevel::Debug,
            ("imgLoader::ValidateEntry BYPASSING cache validation for %s "
             "because of NULL loading document",
             aURI->GetSpecOrDefault().get()));
  }

  // If the original request is still transferring don't kick off a validation
  // network request because it is a bit silly to issue a validation request if
  // the original request hasn't even finished yet. So just return true
  // indicating the caller can create a new proxy for the request and use it as
  // is.
  // This is an optimization but it's also required for correctness. If we don't
  // do this then when firing the load complete notification for the original
  // request that can unblock load for the document and then spin the event loop
  // (see the stack in bug 1641682) which then the OnStartRequest for the
  // validation request can fire which can call UpdateProxies and can sync
  // notify on the progress tracker about all existing state, which includes
  // load complete, so we fire a second load complete notification for the
  // image.
  // In addition, we want to validate if the original request encountered
  // an error for two reasons. The first being if the error was a network error
  // then trying to re-fetch the image might succeed. The second is more
  // complicated. We decide if we should fire the load or error event for img
  // elements depending on if the image has error in its status at the time when
  // the load complete notification is received, and we set error status on an
  // image if it encounters a network error or a decode error with no real way
  // to tell them apart. So if we load an image that will produce a decode error
  // the first time we will usually fire the load event, and then decode enough
  // to encounter the decode error and set the error status on the image. The
  // next time we reference the image in the same document the load complete
  // notification is replayed and this time the error status from the decode is
  // already present so we fire the error event instead of the load event. This
  // is a bug (bug 1645576) that we should fix. In order to avoid that bug in
  // some cases (specifically the cases when we hit this code and try to
  // validate the request) we make sure to validate. This avoids the bug because
  // when the load complete notification arrives the proxy is marked as
  // validating so it lies about its status and returns nothing.
  const bool requestComplete = [&] {
    RefPtr<ProgressTracker> tracker;
    RefPtr<mozilla::image::Image> image = request->GetImage();
    if (image) {
      tracker = image->GetProgressTracker();
    } else {
      tracker = request->GetProgressTracker();
    }
    return tracker &&
           tracker->GetProgress() & (FLAG_LOAD_COMPLETE | FLAG_HAS_ERROR);
  }();

  if (!requestComplete) {
    return true;
  }

  if (validateRequest && aCanMakeNewChannel) {
    LOG_SCOPE(gImgLog, "imgLoader::ValidateRequest |cache hit| must validate");

    uint64_t innerWindowID =
        aLoadingDocument ? aLoadingDocument->InnerWindowID() : 0;
    return ValidateRequestWithNewChannel(
        request, aURI, aInitialDocumentURI, aReferrerInfo, aLoadGroup,
        aObserver, aLoadingDocument, innerWindowID, aLoadFlags, aLoadPolicyType,
        aProxyRequest, aTriggeringPrincipal, aCORSMode, aLinkPreload,
        aEarlyHintPreloaderId, aFetchPriority, aNewChannelCreated);
  }

  if (!validateRequest) {
    NotifyObserversForCachedImage(
        aEntry, request, aURI, aReferrerInfo, aLoadingDocument,
        aTriggeringPrincipal, aCORSMode, aEarlyHintPreloaderId, aFetchPriority);
  }

  return !validateRequest;
}

bool imgLoader::RemoveFromCache(const ImageCacheKey& aKey) {
  LOG_STATIC_FUNC_WITH_PARAM(gImgLog, "imgLoader::RemoveFromCache", "uri",
                             aKey.URI());
  RefPtr<imgCacheEntry> entry;
  mCache.Remove(aKey, getter_AddRefs(entry));
  if (entry) {
    MOZ_ASSERT(!entry->Evicted(), "Evicting an already-evicted cache entry!");

    // Entries with no proxies are in the tracker.
    if (entry->HasNoProxies()) {
      if (mCacheTracker) {
        mCacheTracker->RemoveObject(entry);
      }
      mCacheQueue.Remove(entry);
    }

    entry->SetEvicted(true);

    RefPtr<imgRequest> request = entry->GetRequest();
    request->SetIsInCache(false);
    AddToUncachedImages(request);

    return true;
  }
  return false;
}

bool imgLoader::RemoveFromCache(imgCacheEntry* entry, QueueState aQueueState) {
  LOG_STATIC_FUNC(gImgLog, "imgLoader::RemoveFromCache entry");

  RefPtr<imgRequest> request = entry->GetRequest();
  if (request) {
    const ImageCacheKey& key = request->CacheKey();
    LOG_STATIC_FUNC_WITH_PARAM(gImgLog, "imgLoader::RemoveFromCache",
                               "entry's uri", key.URI());

    mCache.Remove(key);

    if (entry->HasNoProxies()) {
      LOG_STATIC_FUNC(gImgLog,
                      "imgLoader::RemoveFromCache removing from tracker");
      if (mCacheTracker) {
        mCacheTracker->RemoveObject(entry);
      }
      // Only search the queue to remove the entry if its possible it might
      // be in the queue.  If we know its not in the queue this would be
      // wasted work.
      MOZ_ASSERT_IF(aQueueState == QueueState::AlreadyRemoved,
                    !mCacheQueue.Contains(entry));
      if (aQueueState == QueueState::MaybeExists) {
        mCacheQueue.Remove(entry);
      }
    }

    entry->SetEvicted(true);
    request->SetIsInCache(false);
    AddToUncachedImages(request);

    return true;
  }

  return false;
}

nsresult imgLoader::ClearImageCache(ClearOptions aOptions) {
  const bool chromeOnly = aOptions.contains(ClearOption::ChromeOnly);
  const bool contentOnly = aOptions.contains(ClearOption::ContentOnly);
  const auto ShouldRemove = [&](imgCacheEntry* aEntry) {
    if (chromeOnly || contentOnly) {
      RefPtr<imgRequest> request = aEntry->GetRequest();
      if (!request) {
        return false;
      }
      nsIURI* uri = request->CacheKey().URI();
      bool isChrome = uri->SchemeIs("chrome") || uri->SchemeIs("resource");
      if (chromeOnly && !isChrome) {
        return false;
      }
      if (contentOnly && isChrome) {
        return false;
      }
    }
    return true;
  };
  if (aOptions.contains(ClearOption::UnusedOnly)) {
    LOG_STATIC_FUNC(gImgLog, "imgLoader::ClearImageCache queue");
    // We have to make a temporary, since RemoveFromCache removes the element
    // from the queue, invalidating iterators.
    nsTArray<RefPtr<imgCacheEntry>> entries(mCacheQueue.GetNumElements());
    for (auto& entry : mCacheQueue) {
      if (ShouldRemove(entry)) {
        entries.AppendElement(entry);
      }
    }

    // Iterate in reverse order to minimize array copying.
    for (auto& entry : entries) {
      if (!RemoveFromCache(entry)) {
        return NS_ERROR_FAILURE;
      }
    }

    MOZ_ASSERT(chromeOnly || contentOnly || mCacheQueue.GetNumElements() == 0);
    return NS_OK;
  }

  LOG_STATIC_FUNC(gImgLog, "imgLoader::ClearImageCache table");
  // We have to make a temporary, since RemoveFromCache removes the element
  // from the queue, invalidating iterators.
  const auto entries =
      ToTArray<nsTArray<RefPtr<imgCacheEntry>>>(mCache.Values());
  for (const auto& entry : entries) {
    if (!ShouldRemove(entry)) {
      continue;
    }
    if (!RemoveFromCache(entry)) {
      return NS_ERROR_FAILURE;
    }
  }
  MOZ_ASSERT(chromeOnly || contentOnly || mCache.IsEmpty());
  return NS_OK;
}

void imgLoader::AddToUncachedImages(imgRequest* aRequest) {
  MutexAutoLock lock(mUncachedImagesMutex);
  mUncachedImages.Insert(aRequest);
}

void imgLoader::RemoveFromUncachedImages(imgRequest* aRequest) {
  MutexAutoLock lock(mUncachedImagesMutex);
  mUncachedImages.Remove(aRequest);
}

#define LOAD_FLAGS_CACHE_MASK \
  (nsIRequest::LOAD_BYPASS_CACHE | nsIRequest::LOAD_FROM_CACHE)

#define LOAD_FLAGS_VALIDATE_MASK                              \
  (nsIRequest::VALIDATE_ALWAYS | nsIRequest::VALIDATE_NEVER | \
   nsIRequest::VALIDATE_ONCE_PER_SESSION)

NS_IMETHODIMP
imgLoader::LoadImageXPCOM(
    nsIURI* aURI, nsIURI* aInitialDocumentURI, nsIReferrerInfo* aReferrerInfo,
    nsIPrincipal* aTriggeringPrincipal, nsILoadGroup* aLoadGroup,
    imgINotificationObserver* aObserver, Document* aLoadingDocument,
    nsLoadFlags aLoadFlags, nsISupports* aCacheKey,
    nsContentPolicyType aContentPolicyType, imgIRequest** _retval) {
  // Optional parameter, so defaults to 0 (== TYPE_INVALID)
  if (!aContentPolicyType) {
    aContentPolicyType = nsIContentPolicy::TYPE_INTERNAL_IMAGE;
  }
  imgRequestProxy* proxy;
  nsresult rv =
      LoadImage(aURI, aInitialDocumentURI, aReferrerInfo, aTriggeringPrincipal,
                0, aLoadGroup, aObserver, aLoadingDocument, aLoadingDocument,
                aLoadFlags, aCacheKey, aContentPolicyType, u""_ns,
                /* aUseUrgentStartForChannel */ false, /* aListPreload */ false,
                0, FetchPriority::Auto, &proxy);
  *_retval = proxy;
  return rv;
}

static void MakeRequestStaticIfNeeded(
    Document* aLoadingDocument, imgRequestProxy** aProxyAboutToGetReturned) {
  if (!aLoadingDocument || !aLoadingDocument->IsStaticDocument()) {
    return;
  }

  if (!*aProxyAboutToGetReturned) {
    return;
  }

  RefPtr<imgRequestProxy> proxy = dont_AddRef(*aProxyAboutToGetReturned);
  *aProxyAboutToGetReturned = nullptr;

  RefPtr<imgRequestProxy> staticProxy =
      proxy->GetStaticRequest(aLoadingDocument);
  if (staticProxy != proxy) {
    proxy->CancelAndForgetObserver(NS_BINDING_ABORTED);
    proxy = std::move(staticProxy);
  }
  proxy.forget(aProxyAboutToGetReturned);
}

bool imgLoader::IsImageAvailable(nsIURI* aURI,
                                 nsIPrincipal* aTriggeringPrincipal,
                                 CORSMode aCORSMode, Document* aDocument) {
  ImageCacheKey key(aURI, aCORSMode, aDocument);
  RefPtr<imgCacheEntry> entry;
  if (!mCache.Get(key, getter_AddRefs(entry)) || !entry) {
    return false;
  }
  RefPtr<imgRequest> request = entry->GetRequest();
  if (!request) {
    return false;
  }
  if (nsCOMPtr<nsILoadGroup> docLoadGroup = aDocument->GetDocumentLoadGroup()) {
    nsLoadFlags requestFlags = nsIRequest::LOAD_NORMAL;
    docLoadGroup->GetLoadFlags(&requestFlags);
    if (requestFlags & nsIRequest::LOAD_BYPASS_CACHE) {
      // If we're bypassing the cache, treat the image as not available.
      return false;
    }
  }
  return ValidateCORSMode(request, false, aCORSMode, aTriggeringPrincipal);
}

nsresult imgLoader::LoadImage(
    nsIURI* aURI, nsIURI* aInitialDocumentURI, nsIReferrerInfo* aReferrerInfo,
    nsIPrincipal* aTriggeringPrincipal, uint64_t aRequestContextID,
    nsILoadGroup* aLoadGroup, imgINotificationObserver* aObserver,
    nsINode* aContext, Document* aLoadingDocument, nsLoadFlags aLoadFlags,
    nsISupports* aCacheKey, nsContentPolicyType aContentPolicyType,
    const nsAString& initiatorType, bool aUseUrgentStartForChannel,
    bool aLinkPreload, uint64_t aEarlyHintPreloaderId,
    FetchPriority aFetchPriority, imgRequestProxy** _retval) {
  VerifyCacheSizes();

  NS_ASSERTION(aURI, "imgLoader::LoadImage -- NULL URI pointer");

  if (!aURI) {
    return NS_ERROR_NULL_POINTER;
  }

  auto makeStaticIfNeeded = mozilla::MakeScopeExit(
      [&] { MakeRequestStaticIfNeeded(aLoadingDocument, _retval); });

  AUTO_PROFILER_LABEL_DYNAMIC_NSCSTRING("imgLoader::LoadImage", NETWORK,
                                        aURI->GetSpecOrDefault());

  LOG_SCOPE_WITH_PARAM(gImgLog, "imgLoader::LoadImage", "aURI", aURI);

  *_retval = nullptr;

  RefPtr<imgRequest> request;

  nsresult rv;
  nsLoadFlags requestFlags = nsIRequest::LOAD_NORMAL;

#ifdef DEBUG
  bool isPrivate = false;

  if (aLoadingDocument) {
    isPrivate = aLoadingDocument->IsInPrivateBrowsing();
  } else if (aLoadGroup) {
    isPrivate = nsContentUtils::IsInPrivateBrowsing(aLoadGroup);
  }
  MOZ_ASSERT(isPrivate == mRespectPrivacy);

  if (aLoadingDocument) {
    // The given load group should match that of the document if given. If
    // that isn't the case, then we need to add more plumbing to ensure we
    // block the document as well.
    nsCOMPtr<nsILoadGroup> docLoadGroup =
        aLoadingDocument->GetDocumentLoadGroup();
    MOZ_ASSERT(docLoadGroup == aLoadGroup);
  }
#endif

  // Get the default load flags from the loadgroup (if possible)...
  if (aLoadGroup) {
    aLoadGroup->GetLoadFlags(&requestFlags);
  }
  //
  // Merge the default load flags with those passed in via aLoadFlags.
  // Currently, *only* the caching, validation and background load flags
  // are merged...
  //
  // The flags in aLoadFlags take precedence over the default flags!
  //
  if (aLoadFlags & LOAD_FLAGS_CACHE_MASK) {
    // Override the default caching flags...
    requestFlags = (requestFlags & ~LOAD_FLAGS_CACHE_MASK) |
                   (aLoadFlags & LOAD_FLAGS_CACHE_MASK);
  }
  if (aLoadFlags & LOAD_FLAGS_VALIDATE_MASK) {
    // Override the default validation flags...
    requestFlags = (requestFlags & ~LOAD_FLAGS_VALIDATE_MASK) |
                   (aLoadFlags & LOAD_FLAGS_VALIDATE_MASK);
  }
  if (aLoadFlags & nsIRequest::LOAD_BACKGROUND) {
    // Propagate background loading...
    requestFlags |= nsIRequest::LOAD_BACKGROUND;
  }

  if (aLinkPreload) {
    // Set background loading if it is <link rel=preload>
    requestFlags |= nsIRequest::LOAD_BACKGROUND;
  }

  CORSMode corsmode = CORS_NONE;
  if (aLoadFlags & imgILoader::LOAD_CORS_ANONYMOUS) {
    corsmode = CORS_ANONYMOUS;
  } else if (aLoadFlags & imgILoader::LOAD_CORS_USE_CREDENTIALS) {
    corsmode = CORS_USE_CREDENTIALS;
  }

  // Look in the preloaded images of loading document first.
  if (!aLinkPreload && aLoadingDocument) {
    // All Early Hints preloads are Link preloads, therefore we don't have a
    // Early Hints preload here
    MOZ_ASSERT(!aEarlyHintPreloaderId);
    auto key =
        PreloadHashKey::CreateAsImage(aURI, aTriggeringPrincipal, corsmode);
    if (RefPtr<PreloaderBase> preload =
            aLoadingDocument->Preloads().LookupPreload(key)) {
      RefPtr<imgRequestProxy> proxy = do_QueryObject(preload);
      MOZ_ASSERT(proxy);

      MOZ_LOG(gImgLog, LogLevel::Debug,
              ("[this=%p] imgLoader::LoadImage -- preloaded [proxy=%p]"
               " [document=%p]\n",
               this, proxy.get(), aLoadingDocument));

      // Removing the preload for this image to be in parity with Chromium.  Any
      // following regular image request will be reloaded using the regular
      // path: image cache, http cache, network.  Any following `<link
      // rel=preload as=image>` will start a new image preload that can be
      // satisfied from http cache or network.
      //
      // There is a spec discussion for "preload cache", see
      // https://github.com/w3c/preload/issues/97. And it is also not clear how
      // preload image interacts with list of available images, see
      // https://github.com/whatwg/html/issues/4474.
      proxy->RemoveSelf(aLoadingDocument);
      proxy->NotifyUsage(aLoadingDocument);

      imgRequest* request = proxy->GetOwner();
      nsresult rv =
          CreateNewProxyForRequest(request, aURI, aLoadGroup, aLoadingDocument,
                                   aObserver, requestFlags, _retval);
      NS_ENSURE_SUCCESS(rv, rv);

      imgRequestProxy* newProxy = *_retval;
      if (imgCacheValidator* validator = request->GetValidator()) {
        newProxy->MarkValidating();
        // Attach the proxy without notifying and this will add us to the load
        // group.
        validator->AddProxy(newProxy);
      } else {
        // It's OK to add here even if the request is done. If it is, it'll send
        // a OnStopRequest()and the proxy will be removed from the loadgroup in
        // imgRequestProxy::OnLoadComplete.
        newProxy->AddToLoadGroup();
        newProxy->NotifyListener();
      }

      return NS_OK;
    }
  }

  RefPtr<imgCacheEntry> entry;

  // Look in the cache for our URI, and then validate it.
  // XXX For now ignore aCacheKey. We will need it in the future
  // for correctly dealing with image load requests that are a result
  // of post data.
  ImageCacheKey key(aURI, corsmode, aLoadingDocument);
  if (mCache.Get(key, getter_AddRefs(entry)) && entry) {
    bool newChannelCreated = false;
    if (ValidateEntry(entry, aURI, aInitialDocumentURI, aReferrerInfo,
                      aLoadGroup, aObserver, aLoadingDocument, requestFlags,
                      aContentPolicyType, true, &newChannelCreated, _retval,
                      aTriggeringPrincipal, corsmode, aLinkPreload,
                      aEarlyHintPreloaderId, aFetchPriority)) {
      request = entry->GetRequest();

      // If this entry has no proxies, its request has no reference to the
      // entry.
      if (entry->HasNoProxies()) {
        LOG_FUNC_WITH_PARAM(gImgLog,
                            "imgLoader::LoadImage() adding proxyless entry",
                            "uri", key.URI());
        MOZ_ASSERT(!request->HasCacheEntry(),
                   "Proxyless entry's request has cache entry!");
        request->SetCacheEntry(entry);

        if (mCacheTracker && entry->GetExpirationState()->IsTracked()) {
          mCacheTracker->MarkUsed(entry);
        }
      }

      entry->Touch();

      if (!newChannelCreated) {
        // This is ugly but it's needed to report CSP violations. We have 3
        // scenarios:
        // - we don't have cache. We are not in this if() stmt. A new channel is
        //   created and that triggers the CSP checks.
        // - We have a cache entry and this is blocked by CSP directives.
        DebugOnly<bool> shouldLoad = ShouldLoadCachedImage(
            request, aLoadingDocument, aTriggeringPrincipal, aContentPolicyType,
            /* aSendCSPViolationReports */ true);
        MOZ_ASSERT(shouldLoad);
      }
    } else {
      // We can't use this entry. We'll try to load it off the network, and if
      // successful, overwrite the old entry in the cache with a new one.
      entry = nullptr;
    }
  }

  // Keep the channel in this scope, so we can adjust its notificationCallbacks
  // later when we create the proxy.
  nsCOMPtr<nsIChannel> newChannel;
  // If we didn't get a cache hit, we need to load from the network.
  if (!request) {
    LOG_SCOPE(gImgLog, "imgLoader::LoadImage |cache miss|");

    bool forcePrincipalCheck;
    rv = NewImageChannel(getter_AddRefs(newChannel), &forcePrincipalCheck, aURI,
                         aInitialDocumentURI, corsmode, aReferrerInfo,
                         aLoadGroup, requestFlags, aContentPolicyType,
                         aTriggeringPrincipal, aContext, mRespectPrivacy,
                         aEarlyHintPreloaderId, aFetchPriority, aLinkPreload);
    if (NS_FAILED(rv)) {
      return NS_ERROR_FAILURE;
    }

    MOZ_ASSERT(NS_UsePrivateBrowsing(newChannel) == mRespectPrivacy);

    NewRequestAndEntry(forcePrincipalCheck, this, key, getter_AddRefs(request),
                       getter_AddRefs(entry));

    MOZ_LOG(gImgLog, LogLevel::Debug,
            ("[this=%p] imgLoader::LoadImage -- Created new imgRequest"
             " [request=%p]\n",
             this, request.get()));

    nsCOMPtr<nsIClassOfService> cos(do_QueryInterface(newChannel));
    if (cos) {
      if (aUseUrgentStartForChannel && !aLinkPreload) {
        cos->AddClassFlags(nsIClassOfService::UrgentStart);
      }
      if (StaticPrefs::image_priority_incremental()) {
        cos->SetIncremental(true);
      }

      if (StaticPrefs::network_http_tailing_enabled() &&
          aContentPolicyType == nsIContentPolicy::TYPE_INTERNAL_IMAGE_FAVICON) {
        cos->AddClassFlags(nsIClassOfService::Throttleable |
                           nsIClassOfService::Tail);
        nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(newChannel));
        if (httpChannel) {
          (void)httpChannel->SetRequestContextID(aRequestContextID);
        }
      }
    }

    nsCOMPtr<nsILoadGroup> channelLoadGroup;
    newChannel->GetLoadGroup(getter_AddRefs(channelLoadGroup));
    rv = request->Init(aURI, aURI, /* aHadInsecureRedirect = */ false,
                       channelLoadGroup, newChannel, entry, aLoadingDocument,
                       aTriggeringPrincipal, corsmode, aReferrerInfo);
    if (NS_FAILED(rv)) {
      return NS_ERROR_FAILURE;
    }

    // Add the initiator type for this image load
    nsCOMPtr<nsITimedChannel> timedChannel = do_QueryInterface(newChannel);
    if (timedChannel) {
      timedChannel->SetInitiatorType(initiatorType);
    }

    // create the proxy listener
#ifdef NIGHTLY_BUILD
    nsCOMPtr<nsIStreamListener> listener =
        new ProxyListener(request.get(), ShouldEnableWAICT(aLoadingDocument));
#else
    nsCOMPtr<nsIStreamListener> listener = new ProxyListener(request.get());
#endif

    MOZ_LOG(gImgLog, LogLevel::Debug,
            ("[this=%p] imgLoader::LoadImage -- Calling channel->AsyncOpen()\n",
             this));

    nsresult openRes;
    openRes = newChannel->AsyncOpen(listener);

    if (NS_FAILED(openRes)) {
      MOZ_LOG(
          gImgLog, LogLevel::Debug,
          ("[this=%p] imgLoader::LoadImage -- AsyncOpen() failed: 0x%" PRIx32
           "\n",
           this, static_cast<uint32_t>(openRes)));
      request->CancelAndAbort(openRes);
      return openRes;
    }

    // Try to add the new request into the cache.
    PutIntoCache(key, entry);
  } else {
    LOG_MSG_WITH_PARAM(gImgLog, "imgLoader::LoadImage |cache hit|", "request",
                       request);
  }

  // If we didn't get a proxy when validating the cache entry, we need to
  // create one.
  if (!*_retval) {
    // ValidateEntry() has three return values: "Is valid," "might be valid --
    // validating over network", and "not valid." If we don't have a _retval,
    // we know ValidateEntry is not validating over the network, so it's safe
    // to SetLoadId here because we know this request is valid for this context.
    //
    // Note, however, that this doesn't guarantee the behaviour we want (one
    // URL maps to the same image on a page) if we load the same image in a
    // different tab (see bug 528003), because its load id will get re-set, and
    // that'll cause us to validate over the network.
    request->SetLoadId(aLoadingDocument);

    LOG_MSG(gImgLog, "imgLoader::LoadImage", "creating proxy request.");
    rv = CreateNewProxyForRequest(request, aURI, aLoadGroup, aLoadingDocument,
                                  aObserver, requestFlags, _retval);
    if (NS_FAILED(rv)) {
      return rv;
    }

    imgRequestProxy* proxy = *_retval;

    // Make sure that OnStatus/OnProgress calls have the right request set, if
    // we did create a channel here.
    if (newChannel) {
      nsCOMPtr<nsIInterfaceRequestor> requestor(
          new nsProgressNotificationProxy(newChannel, proxy));
      if (!requestor) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
      newChannel->SetNotificationCallbacks(requestor);
    }

    if (aLinkPreload) {
      MOZ_ASSERT(aLoadingDocument);
      auto preloadKey =
          PreloadHashKey::CreateAsImage(aURI, aTriggeringPrincipal, corsmode);
      proxy->NotifyOpen(preloadKey, aLoadingDocument, true);
    }

    // Note that it's OK to add here even if the request is done.  If it is,
    // it'll send a OnStopRequest() to the proxy in imgRequestProxy::Notify and
    // the proxy will be removed from the loadgroup.
    proxy->AddToLoadGroup();

    // If we're loading off the network, explicitly don't notify our proxy,
    // because necko (or things called from necko, such as imgCacheValidator)
    // are going to call our notifications asynchronously, and we can't make it
    // further asynchronous because observers might rely on imagelib completing
    // its work between the channel's OnStartRequest and OnStopRequest.
    if (!newChannel) {
      proxy->NotifyListener();
    }

    return rv;
  }

  NS_ASSERTION(*_retval, "imgLoader::LoadImage -- no return value");

  return NS_OK;
}

NS_IMETHODIMP
imgLoader::LoadImageWithChannelXPCOM(nsIChannel* channel,
                                     imgINotificationObserver* aObserver,
                                     Document* aLoadingDocument,
                                     nsIStreamListener** listener,
                                     imgIRequest** _retval) {
  nsresult result;
  imgRequestProxy* proxy;
  result = LoadImageWithChannel(channel, aObserver, aLoadingDocument, listener,
                                &proxy);
  *_retval = proxy;
  return result;
}

nsresult imgLoader::LoadImageWithChannel(nsIChannel* channel,
                                         imgINotificationObserver* aObserver,
                                         Document* aLoadingDocument,
                                         nsIStreamListener** listener,
                                         imgRequestProxy** _retval) {
  NS_ASSERTION(channel,
               "imgLoader::LoadImageWithChannel -- NULL channel pointer");

  MOZ_ASSERT(NS_UsePrivateBrowsing(channel) == mRespectPrivacy);

  auto makeStaticIfNeeded = mozilla::MakeScopeExit(
      [&] { MakeRequestStaticIfNeeded(aLoadingDocument, _retval); });

  LOG_SCOPE(gImgLog, "imgLoader::LoadImageWithChannel");
  RefPtr<imgRequest> request;

  nsCOMPtr<nsIURI> uri;
  channel->GetURI(getter_AddRefs(uri));

  NS_ENSURE_TRUE(channel, NS_ERROR_FAILURE);
  nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();

  // TODO: Get a meaningful cors mode from the caller probably?
  const auto corsMode = CORS_NONE;
  ImageCacheKey key(uri, corsMode, aLoadingDocument);

  nsLoadFlags requestFlags = nsIRequest::LOAD_NORMAL;
  channel->GetLoadFlags(&requestFlags);

  RefPtr<imgCacheEntry> entry;

  if (requestFlags & nsIRequest::LOAD_BYPASS_CACHE) {
    RemoveFromCache(key);
  } else {
    // Look in the cache for our URI, and then validate it.
    // XXX For now ignore aCacheKey. We will need it in the future
    // for correctly dealing with image load requests that are a result
    // of post data.
    if (mCache.Get(key, getter_AddRefs(entry)) && entry) {
      // We don't want to kick off another network load. So we ask
      // ValidateEntry to only do validation without creating a new proxy. If
      // it says that the entry isn't valid any more, we'll only use the entry
      // we're getting if the channel is loading from the cache anyways.
      //
      // XXX -- should this be changed? it's pretty much verbatim from the old
      // code, but seems nonsensical.
      //
      // Since aCanMakeNewChannel == false, we don't need to pass content policy
      // type/principal/etc

      nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
      // if there is a loadInfo, use the right contentType, otherwise
      // default to the internal image type
      nsContentPolicyType policyType = loadInfo->InternalContentPolicyType();

      if (ValidateEntry(entry, uri, nullptr, nullptr, nullptr, aObserver,
                        aLoadingDocument, requestFlags, policyType, false,
                        nullptr, nullptr, nullptr, corsMode, false, 0,
                        FetchPriority::Auto)) {
        request = entry->GetRequest();
      } else {
        nsCOMPtr<nsICacheInfoChannel> cacheChan(do_QueryInterface(channel));
        bool bUseCacheCopy;

        if (cacheChan) {
          cacheChan->IsFromCache(&bUseCacheCopy);
        } else {
          bUseCacheCopy = false;
        }

        if (!bUseCacheCopy) {
          entry = nullptr;
        } else {
          request = entry->GetRequest();
        }
      }

      if (request && entry) {
        // If this entry has no proxies, its request has no reference to
        // the entry.
        if (entry->HasNoProxies()) {
          LOG_FUNC_WITH_PARAM(
              gImgLog,
              "imgLoader::LoadImageWithChannel() adding proxyless entry", "uri",
              key.URI());
          MOZ_ASSERT(!request->HasCacheEntry(),
                     "Proxyless entry's request has cache entry!");
          request->SetCacheEntry(entry);

          if (mCacheTracker && entry->GetExpirationState()->IsTracked()) {
            mCacheTracker->MarkUsed(entry);
          }
        }
      }
    }
  }

  nsCOMPtr<nsILoadGroup> loadGroup;
  channel->GetLoadGroup(getter_AddRefs(loadGroup));

#ifdef DEBUG
  if (aLoadingDocument) {
    // The load group of the channel should always match that of the
    // document if given. If that isn't the case, then we need to add more
    // plumbing to ensure we block the document as well.
    nsCOMPtr<nsILoadGroup> docLoadGroup =
        aLoadingDocument->GetDocumentLoadGroup();
    MOZ_ASSERT(docLoadGroup == loadGroup);
  }
#endif

  // Filter out any load flags not from nsIRequest
  requestFlags &= nsIRequest::LOAD_INHERIT_MASK;

  nsresult rv = NS_OK;
  if (request) {
    // we have this in our cache already.. cancel the current (document) load

    // this should fire an OnStopRequest
    channel->Cancel(NS_ERROR_PARSED_DATA_CACHED);

    *listener = nullptr;  // give them back a null nsIStreamListener

    rv = CreateNewProxyForRequest(request, uri, loadGroup, aLoadingDocument,
                                  aObserver, requestFlags, _retval);
    static_cast<imgRequestProxy*>(*_retval)->NotifyListener();
  } else {
    // We use originalURI here to fulfil the imgIRequest contract on GetURI.
    nsCOMPtr<nsIURI> originalURI;
    channel->GetOriginalURI(getter_AddRefs(originalURI));

    // XXX(seth): We should be able to just use |key| here, except that |key| is
    // constructed above with the *current URI* and not the *original URI*. I'm
    // pretty sure this is a bug, and it's preventing us from ever getting a
    // cache hit in LoadImageWithChannel when redirects are involved.
    ImageCacheKey originalURIKey(originalURI, corsMode, aLoadingDocument);

    // Default to doing a principal check because we don't know who
    // started that load and whether their principal ended up being
    // inherited on the channel.
    NewRequestAndEntry(/* aForcePrincipalCheckForCacheEntry = */ true, this,
                       originalURIKey, getter_AddRefs(request),
                       getter_AddRefs(entry));

    // No principal specified here, because we're not passed one.
    // In LoadImageWithChannel, the redirects that may have been
    // associated with this load would have gone through necko.
    // We only have the final URI in ImageLib and hence don't know
    // if the request went through insecure redirects.  But if it did,
    // the necko cache should have handled that (since all necko cache hits
    // including the redirects will go through content policy).  Hence, we
    // can set aHadInsecureRedirect to false here.
    rv = request->Init(originalURI, uri, /* aHadInsecureRedirect = */ false,
                       channel, channel, entry, aLoadingDocument, nullptr,
                       corsMode, nullptr);
    NS_ENSURE_SUCCESS(rv, rv);

#ifdef NIGHTLY_BUILD
    RefPtr<ProxyListener> pl =
        new ProxyListener(static_cast<nsIStreamListener*>(request.get()),
                          ShouldEnableWAICT(aLoadingDocument));
#else
    RefPtr<ProxyListener> pl =
        new ProxyListener(static_cast<nsIStreamListener*>(request.get()));
#endif
    pl.forget(listener);

    // Try to add the new request into the cache.
    PutIntoCache(originalURIKey, entry);

    rv = CreateNewProxyForRequest(request, originalURI, loadGroup,
                                  aLoadingDocument, aObserver, requestFlags,
                                  _retval);

    // Explicitly don't notify our proxy, because we're loading off the
    // network, and necko (or things called from necko, such as
    // imgCacheValidator) are going to call our notifications asynchronously,
    // and we can't make it further asynchronous because observers might rely
    // on imagelib completing its work between the channel's OnStartRequest and
    // OnStopRequest.
  }

  if (NS_FAILED(rv)) {
    return rv;
  }

  (*_retval)->AddToLoadGroup();
  return rv;
}

bool imgLoader::SupportImageWithMimeType(const nsACString& aMimeType,
                                         AcceptedMimeTypes aAccept
                                         /* = AcceptedMimeTypes::IMAGES */) {
  nsAutoCString mimeType(aMimeType);
  ToLowerCase(mimeType);

  if (aAccept == AcceptedMimeTypes::IMAGES_AND_DOCUMENTS &&
      mimeType.EqualsLiteral(IMAGE_SVG_XML)) {
    return true;
  }

  DecoderType type = DecoderFactory::GetDecoderType(mimeType.get());
  return type != DecoderType::UNKNOWN;
}

NS_IMETHODIMP
imgLoader::GetMIMETypeFromContent(nsIRequest* aRequest,
                                  const uint8_t* aContents, uint32_t aLength,
                                  nsACString& aContentType) {
  nsCOMPtr<nsIChannel> channel(do_QueryInterface(aRequest));
  if (channel) {
    nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
    if (loadInfo->GetSkipContentSniffing()) {
      return NS_ERROR_NOT_AVAILABLE;
    }
  }

  nsresult rv =
      GetMimeTypeFromContent((const char*)aContents, aLength, aContentType);
  if (NS_SUCCEEDED(rv) && channel && XRE_IsParentProcess()) {
    if (RefPtr<mozilla::net::nsHttpChannel> httpChannel =
            do_QueryObject(channel)) {
      // If the image type pattern matching algorithm given bytes does not
      // return undefined, then disable the further check and allow the
      // response.
      httpChannel->DisableIsOpaqueResponseAllowedAfterSniffCheck(
          mozilla::net::nsHttpChannel::SnifferType::Image);
    }
  }

  return rv;
}

/* static */
nsresult imgLoader::GetMimeTypeFromContent(const char* aContents,
                                           uint32_t aLength,
                                           nsACString& aContentType) {
  nsAutoCString detected;

  /* Is it a GIF? */
  if (aLength >= 6 &&
      (!strncmp(aContents, "GIF87a", 6) || !strncmp(aContents, "GIF89a", 6))) {
    aContentType.AssignLiteral(IMAGE_GIF);

    /* or a PNG? */
  } else if (aLength >= 8 && ((unsigned char)aContents[0] == 0x89 &&
                              (unsigned char)aContents[1] == 0x50 &&
                              (unsigned char)aContents[2] == 0x4E &&
                              (unsigned char)aContents[3] == 0x47 &&
                              (unsigned char)aContents[4] == 0x0D &&
                              (unsigned char)aContents[5] == 0x0A &&
                              (unsigned char)aContents[6] == 0x1A &&
                              (unsigned char)aContents[7] == 0x0A)) {
    aContentType.AssignLiteral(IMAGE_PNG);

    /* maybe a JPEG (JFIF)? */
    /* JFIF files start with SOI APP0 but older files can start with SOI DQT
     * so we test for SOI followed by any marker, i.e. FF D8 FF
     * this will also work for SPIFF JPEG files if they appear in the future.
     *
     * (JFIF is 0XFF 0XD8 0XFF 0XE0 <skip 2> 0X4A 0X46 0X49 0X46 0X00)
     */
  } else if (aLength >= 3 && ((unsigned char)aContents[0]) == 0xFF &&
             ((unsigned char)aContents[1]) == 0xD8 &&
             ((unsigned char)aContents[2]) == 0xFF) {
    aContentType.AssignLiteral(IMAGE_JPEG);

    /* or how about ART? */
    /* ART begins with JG (4A 47). Major version offset 2.
     * Minor version offset 3. Offset 4 must be nullptr.
     */
  } else if (aLength >= 5 && ((unsigned char)aContents[0]) == 0x4a &&
             ((unsigned char)aContents[1]) == 0x47 &&
             ((unsigned char)aContents[4]) == 0x00) {
    aContentType.AssignLiteral(IMAGE_ART);

  } else if (aLength >= 2 && !strncmp(aContents, "BM", 2)) {
    aContentType.AssignLiteral(IMAGE_BMP);

    // ICOs always begin with a 2-byte 0 followed by a 2-byte 1.
    // CURs begin with 2-byte 0 followed by 2-byte 2.
  } else if (aLength >= 4 && (!memcmp(aContents, "\000\000\001\000", 4) ||
                              !memcmp(aContents, "\000\000\002\000", 4))) {
    aContentType.AssignLiteral(IMAGE_ICO);

    // WebPs always begin with RIFF, a 32-bit length, and WEBP.
  } else if (aLength >= 12 && !memcmp(aContents, "RIFF", 4) &&
             !memcmp(aContents + 8, "WEBP", 4)) {
    aContentType.AssignLiteral(IMAGE_WEBP);

  } else if (MatchesMP4(reinterpret_cast<const uint8_t*>(aContents), aLength,
                        detected) &&
             detected.Equals(IMAGE_AVIF)) {
    aContentType.AssignLiteral(IMAGE_AVIF);
  } else if ((aLength >= 2 && !memcmp(aContents, "\xFF\x0A", 2)) ||
             (aLength >= 12 &&
              !memcmp(aContents, "\x00\x00\x00\x0CJXL \x0D\x0A\x87\x0A", 12))) {
    // Each version is for containerless and containerful files respectively.
    aContentType.AssignLiteral(IMAGE_JXL);
  } else {
    /* none of the above?  I give up */
    return NS_ERROR_NOT_AVAILABLE;
  }

  return NS_OK;
}

/**
 * proxy stream listener class used to handle multipart/x-mixed-replace
 */

#include "nsIRequest.h"
#include "nsIStreamConverterService.h"

NS_IMPL_ISUPPORTS(ProxyListener, nsIStreamListener,
                  nsIThreadRetargetableStreamListener, nsIRequestObserver)

ProxyListener::ProxyListener(nsIStreamListener* dest) : mDestListener(dest) {}

#ifdef NIGHTLY_BUILD
ProxyListener::ProxyListener(nsIStreamListener* dest, bool aIsWAICTEnabled)
    : mDestListener(dest), mIsWAICTEnabled(aIsWAICTEnabled) {}
#endif

ProxyListener::~ProxyListener() = default;

#ifdef NIGHTLY_BUILD
static bool ShouldEnableWAICT(Document* aDoc) {
  if (!StaticPrefs::security_waict_enabled()) {
    return false;
  }
  if (!aDoc) {
    return false;
  }
  auto* policy =
      PolicyContainer::GetIntegrityPolicyWAICT(aDoc->GetPolicyContainer());
  return policy &&
         policy->ShouldHandle(IntegrityPolicy::DestinationType::Image);
}

// Reads and buffers image data from aInStr into aBufferedImage while updating
// the hash. Returns false if the buffer limit
// (security.waict.allowed_image_storage) was exceeded.
static bool MaybeUpdateWAICTHash(mozilla::dom::ResourceHasher* aHasher,
                                 nsTArray<uint8_t>& aBufferedImage,
                                 nsIInputStream* aInStr, uint32_t aCount) {
  MOZ_ASSERT(aHasher);
  uint32_t prevLen = aBufferedImage.Length();
  if ((uint64_t)prevLen + aCount >
      StaticPrefs::security_waict_allowed_image_storage()) {
    return false;
  }
  aBufferedImage.SetLength(prevLen + aCount);
  uint32_t bytesRead = 0;
  nsresult rv =
      aInStr->Read(reinterpret_cast<char*>(aBufferedImage.Elements() + prevLen),
                   aCount, &bytesRead);
  if (NS_FAILED(rv) || bytesRead == 0) {
    aBufferedImage.SetLength(prevLen);
    return true;
  }
  aBufferedImage.SetLength(prevLen + bytesRead);
  aHasher->Update(aBufferedImage.Elements() + prevLen, bytesRead);
  return true;
}

static bool MaybeCheckWAICTIntegrity(nsIStreamListener* aListener,
                                     mozilla::dom::ResourceHasher* aHasher,
                                     nsIRequest* aRequest, nsresult& aStatus,
                                     nsTArray<uint8_t> aBufferedImage) {
  if (!aHasher) {
    return false;
  }
  nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
  if (!channel) {
    return false;
  }
  aHasher->Finish();
  nsCString computedHash(aHasher->GetHash());
  if (NS_WARN_IF(computedHash.IsEmpty())) {
    aStatus = NS_ERROR_FAILURE;
    return false;
  }
  nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
  nsCOMPtr<nsISupports> loadingContext = loadInfo->GetLoadingContext();
  RefPtr<Document> doc;
  if (nsCOMPtr<nsINode> node = do_QueryInterface(loadingContext)) {
    doc = node->OwnerDoc();
  }
  if (!doc) {
    return false;
  }
  auto* policy =
      PolicyContainer::GetIntegrityPolicyWAICT(doc->GetPolicyContainer());
  if (!policy) {
    return false;
  }
  nsresult status = aStatus;
  auto promise = policy->WaitForManifestLoad();
  promise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [listener = nsCOMPtr{aListener}, channel, request = nsCOMPtr{aRequest},
       status, policy = RefPtr{policy}, computedHash = std::move(computedHash),
       bufferedImage = std::move(aBufferedImage)](bool) {
        nsCOMPtr<nsIURI> originalURI;
        channel->GetOriginalURI(getter_AddRefs(originalURI));
        if (!policy->MaybeCheckResourceIntegrity(
                originalURI, IntegrityPolicy::DestinationType::Image,
                computedHash)) {
          return listener->OnStopRequest(request, NS_ERROR_FAILURE);
        }
        uint32_t bufferedImageLen = bufferedImage.Length();
        nsCOMPtr<nsIInputStream> stream;
        NS_NewByteInputStream(getter_AddRefs(stream),
                              mozilla::Span(reinterpret_cast<const char*>(
                                                bufferedImage.Elements()),
                                            bufferedImageLen),
                              NS_ASSIGNMENT_DEPEND);
        if (stream && bufferedImageLen > 0) {
          nsresult rv =
              listener->OnDataAvailable(request, stream, 0, bufferedImageLen);
          if (NS_FAILED(rv)) {
            return listener->OnStopRequest(request, rv);
          }
        }
        return listener->OnStopRequest(request, status);
      },
      [](bool) {
        // WaitForManifestLoad always resolves, never rejects.
        // TODO: Handle edge cases such as page closed before manifest loads.
        MOZ_ASSERT_UNREACHABLE("should always resolve");
      });
  return true;
}
#endif

/** nsIRequestObserver methods **/

NS_IMETHODIMP
ProxyListener::OnStartRequest(nsIRequest* aRequest) {
  if (!mDestListener) {
    return NS_ERROR_FAILURE;
  }

#ifdef NIGHTLY_BUILD
  if (mIsWAICTEnabled) {
    MutexAutoLock lock(mHasherMutex);
    mResourceHasher = mozilla::dom::ResourceHasher::Init();
    if (!mResourceHasher) {
      MOZ_LOG(
          mozilla::waict::gWaictLog, LogLevel::Warning,
          ("ProxyListener::OnStartRequest -- ResourceHasher::Init() failed\n"));
    }
  }
#endif

  nsCOMPtr<nsIChannel> channel(do_QueryInterface(aRequest));
  if (channel) {
    // We need to set the initiator type for the image load
    nsCOMPtr<nsITimedChannel> timedChannel = do_QueryInterface(channel);
    if (timedChannel) {
      nsAutoString type;
      timedChannel->GetInitiatorType(type);
      if (type.IsEmpty()) {
        timedChannel->SetInitiatorType(u"img"_ns);
      }
    }

    nsAutoCString contentType;
    nsresult rv = channel->GetContentType(contentType);

    if (!contentType.IsEmpty()) {
      /* If multipart/x-mixed-replace content, we'll insert a MIME decoder
         in the pipeline to handle the content and pass it along to our
         original listener.
       */
      if ("multipart/x-mixed-replace"_ns.Equals(contentType)) {
        nsCOMPtr<nsIStreamConverterService> convServ(
            do_GetService("@mozilla.org/streamConverters;1", &rv));
        if (NS_SUCCEEDED(rv)) {
          nsCOMPtr<nsIStreamListener> toListener(mDestListener);
          nsCOMPtr<nsIStreamListener> fromListener;

          rv = convServ->AsyncConvertData("multipart/x-mixed-replace", "*/*",
                                          toListener, nullptr,
                                          getter_AddRefs(fromListener));
          if (NS_SUCCEEDED(rv)) {
            mDestListener = std::move(fromListener);
          }
        }
      }
    }
  }

  return mDestListener->OnStartRequest(aRequest);
}

NS_IMETHODIMP
ProxyListener::OnStopRequest(nsIRequest* aRequest, nsresult status) {
  if (!mDestListener) {
    return NS_ERROR_FAILURE;
  }

#ifdef NIGHTLY_BUILD
  if (mIsWAICTEnabled) {
    RefPtr<mozilla::dom::ResourceHasher> hasher;
    nsTArray<uint8_t> bufferedImage;
    {
      MutexAutoLock lock(mHasherMutex);
      hasher = std::move(mResourceHasher);
      bufferedImage = std::move(mBufferedImageWAICT);
    }
    if (MaybeCheckWAICTIntegrity(mDestListener, hasher, aRequest, status,
                                 std::move(bufferedImage))) {
      return NS_OK;
    }
  }
#endif

  return mDestListener->OnStopRequest(aRequest, status);
}

/** nsIStreamListener methods **/

NS_IMETHODIMP
ProxyListener::OnDataAvailable(nsIRequest* aRequest, nsIInputStream* inStr,
                               uint64_t sourceOffset, uint32_t count) {
  if (!mDestListener) {
    return NS_ERROR_FAILURE;
  }

// When WAICT is active, we buffer all image data and defer forwarding to
// mDestListener until integrity has been verified in OnStopRequest. This
// intentionally disables progressive decoding for WAICT-checked images.

// For images not covered by WAICT, the only overhead is this boolean check.
#ifdef NIGHTLY_BUILD
  if (mIsWAICTEnabled) {
    MutexAutoLock lock(mHasherMutex);
    if (mResourceHasher) {
      if (!MaybeUpdateWAICTHash(mResourceHasher, mBufferedImageWAICT, inStr,
                                count)) {
        return NS_ERROR_FAILURE;
      }
      return NS_OK;
    }
    // If hasher init failed, fall through to the normal streaming path.
    // The image will load without WAICT integrity verification.
  }
#endif

  return mDestListener->OnDataAvailable(aRequest, inStr, sourceOffset, count);
}

NS_IMETHODIMP
ProxyListener::OnDataFinished(nsresult aStatus) {
  if (!mDestListener) {
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<nsIThreadRetargetableStreamListener> retargetableListener =
      do_QueryInterface(mDestListener);
  if (retargetableListener) {
    return retargetableListener->OnDataFinished(aStatus);
  }

  return NS_OK;
}

/** nsThreadRetargetableStreamListener methods **/
NS_IMETHODIMP
ProxyListener::CheckListenerChain() {
  NS_ASSERTION(NS_IsMainThread(), "Should be on the main thread!");
  nsresult rv = NS_OK;
  nsCOMPtr<nsIThreadRetargetableStreamListener> retargetableListener =
      do_QueryInterface(mDestListener, &rv);
  if (retargetableListener) {
    rv = retargetableListener->CheckListenerChain();
  }
  MOZ_LOG(
      gImgLog, LogLevel::Debug,
      ("ProxyListener::CheckListenerChain %s [this=%p listener=%p rv=%" PRIx32
       "]",
       (NS_SUCCEEDED(rv) ? "success" : "failure"), this,
       (nsIStreamListener*)mDestListener, static_cast<uint32_t>(rv)));
  return rv;
}

/**
 * http validate class.  check a channel for a 304
 */

NS_IMPL_ISUPPORTS(imgCacheValidator, nsIStreamListener, nsIRequestObserver,
                  nsIThreadRetargetableStreamListener, nsIChannelEventSink,
                  nsIInterfaceRequestor, nsIAsyncVerifyRedirectCallback)

imgCacheValidator::imgCacheValidator(nsProgressNotificationProxy* progress,
                                     imgLoader* loader, imgRequest* request,
                                     Document* aDocument,
                                     uint64_t aInnerWindowId,
                                     bool forcePrincipalCheckForCacheEntry)
    : mProgressProxy(progress),
      mRequest(request),
      mDocument(aDocument),
      mInnerWindowId(aInnerWindowId),
      mImgLoader(loader),
      mHadInsecureRedirect(false) {
  NewRequestAndEntry(forcePrincipalCheckForCacheEntry, loader,
                     mRequest->CacheKey(), getter_AddRefs(mNewRequest),
                     getter_AddRefs(mNewEntry));
}

imgCacheValidator::~imgCacheValidator() {
  if (mRequest) {
    // If something went wrong, and we never unblocked the requests waiting on
    // validation, now is our last chance. We will cancel the new request and
    // switch the waiting proxies to it.
    UpdateProxies(/* aCancelRequest */ true, /* aSyncNotify */ false);
  }
}

void imgCacheValidator::AddProxy(imgRequestProxy* aProxy) {
  // aProxy needs to be in the loadgroup since we're validating from
  // the network.
  aProxy->AddToLoadGroup();

  mProxies.AppendElement(aProxy);
}

void imgCacheValidator::RemoveProxy(imgRequestProxy* aProxy) {
  mProxies.RemoveElement(aProxy);
}

void imgCacheValidator::UpdateProxies(bool aCancelRequest, bool aSyncNotify) {
  MOZ_ASSERT(mRequest);

  // Clear the validator before updating the proxies. The notifications may
  // clone an existing request, and its state could be inconsistent.
  mRequest->SetValidator(nullptr);
  mRequest = nullptr;

  // If an error occurred, we will want to cancel the new request, and make the
  // validating proxies point to it. Any proxies still bound to the original
  // request which are not validating should remain untouched.
  if (aCancelRequest) {
    MOZ_ASSERT(mNewRequest);
    mNewRequest->CancelAndAbort(NS_BINDING_ABORTED);
  }

  // We have finished validating the request, so we can safely take ownership
  // of the proxy list. imgRequestProxy::SyncNotifyListener can mutate the list
  // if imgRequestProxy::CancelAndForgetObserver is called by its owner. Note
  // that any potential notifications should still be suppressed in
  // imgRequestProxy::ChangeOwner because we haven't cleared the validating
  // flag yet, and thus they will remain deferred.
  AutoTArray<RefPtr<imgRequestProxy>, 4> proxies(std::move(mProxies));

  for (auto& proxy : proxies) {
    // First update the state of all proxies before notifying any of them
    // to ensure a consistent state (e.g. in case the notification causes
    // other proxies to be touched indirectly.)
    MOZ_ASSERT(proxy->IsValidating());
    MOZ_ASSERT(proxy->NotificationsDeferred(),
               "Proxies waiting on cache validation should be "
               "deferring notifications!");
    if (mNewRequest) {
      proxy->ChangeOwner(mNewRequest);
    }
    proxy->ClearValidating();
  }

  mNewRequest = nullptr;
  mNewEntry = nullptr;

  for (auto& proxy : proxies) {
    if (aSyncNotify) {
      // Notify synchronously, because the caller knows we are already in an
      // asynchronously-called function (e.g. OnStartRequest).
      proxy->SyncNotifyListener();
    } else {
      // Notify asynchronously, because the caller does not know our current
      // call state (e.g. ~imgCacheValidator).
      proxy->NotifyListener();
    }
  }
}

/** nsIRequestObserver methods **/

NS_IMETHODIMP
imgCacheValidator::OnStartRequest(nsIRequest* aRequest) {
  // We may be holding on to a document, so ensure that it's released.
  RefPtr<Document> document = mDocument.forget();

  // If for some reason we don't still have an existing request (probably
  // because OnStartRequest got delivered more than once), just bail.
  if (!mRequest) {
    MOZ_ASSERT_UNREACHABLE("OnStartRequest delivered more than once?");
    aRequest->CancelWithReason(NS_BINDING_ABORTED,
                               "OnStartRequest delivered more than once?"_ns);
    return NS_ERROR_FAILURE;
  }

  // If this request is coming from cache and has the same URI as our
  // imgRequest, the request all our proxies are pointing at is valid, and all
  // we have to do is tell them to notify their listeners.
  nsCOMPtr<nsICacheInfoChannel> cacheChan(do_QueryInterface(aRequest));
  nsCOMPtr<nsIChannel> channel(do_QueryInterface(aRequest));
  if (cacheChan && channel) {
    bool isFromCache = false;
    cacheChan->IsFromCache(&isFromCache);

    nsCOMPtr<nsIURI> channelURI;
    channel->GetURI(getter_AddRefs(channelURI));

    nsCOMPtr<nsIURI> finalURI;
    mRequest->GetFinalURI(getter_AddRefs(finalURI));

    bool sameURI = false;
    if (channelURI && finalURI) {
      channelURI->Equals(finalURI, &sameURI);
    }

    if (isFromCache && sameURI) {
      // We don't need to load this any more.
      aRequest->CancelWithReason(NS_BINDING_ABORTED,
                                 "imgCacheValidator::OnStartRequest"_ns);
      mNewRequest = nullptr;

      // Clear the validator before updating the proxies. The notifications may
      // clone an existing request, and its state could be inconsistent.
      mRequest->SetLoadId(document);
      mRequest->SetInnerWindowID(mInnerWindowId);
      UpdateProxies(/* aCancelRequest */ false, /* aSyncNotify */ true);
      return NS_OK;
    }
  }

  // We can't load out of cache. We have to create a whole new request for the
  // data that's coming in off the channel.
  nsCOMPtr<nsIURI> uri;
  mRequest->GetURI(getter_AddRefs(uri));

  LOG_MSG_WITH_PARAM(gImgLog,
                     "imgCacheValidator::OnStartRequest creating new request",
                     "uri", uri);

  CORSMode corsmode = mRequest->GetCORSMode();
  nsCOMPtr<nsIReferrerInfo> referrerInfo = mRequest->GetReferrerInfo();
  nsCOMPtr<nsIPrincipal> triggeringPrincipal =
      mRequest->GetTriggeringPrincipal();

  // Doom the old request's cache entry
  mRequest->RemoveFromCache();

  // We use originalURI here to fulfil the imgIRequest contract on GetURI.
  nsCOMPtr<nsIURI> originalURI;
  channel->GetOriginalURI(getter_AddRefs(originalURI));
  nsresult rv = mNewRequest->Init(originalURI, uri, mHadInsecureRedirect,
                                  aRequest, channel, mNewEntry, document,
                                  triggeringPrincipal, corsmode, referrerInfo);
  if (NS_FAILED(rv)) {
    UpdateProxies(/* aCancelRequest */ true, /* aSyncNotify */ true);
    return rv;
  }

#ifdef NIGHTLY_BUILD
  mDestListener = new ProxyListener(mNewRequest, ShouldEnableWAICT(document));
#else
  mDestListener = new ProxyListener(mNewRequest);
#endif

  // Try to add the new request into the cache. Note that the entry must be in
  // the cache before the proxies' ownership changes, because adding a proxy
  // changes the caching behaviour for imgRequests.
  mImgLoader->PutIntoCache(mNewRequest->CacheKey(), mNewEntry);
  UpdateProxies(/* aCancelRequest */ false, /* aSyncNotify */ true);
  return mDestListener->OnStartRequest(aRequest);
}

NS_IMETHODIMP
imgCacheValidator::OnStopRequest(nsIRequest* aRequest, nsresult status) {
  // Be sure we've released the document that we may have been holding on to.
  mDocument = nullptr;

  if (!mDestListener) {
    return NS_OK;
  }

  return mDestListener->OnStopRequest(aRequest, status);
}

/** nsIStreamListener methods **/

NS_IMETHODIMP
imgCacheValidator::OnDataAvailable(nsIRequest* aRequest, nsIInputStream* inStr,
                                   uint64_t sourceOffset, uint32_t count) {
  if (!mDestListener) {
    // XXX see bug 113959
    uint32_t _retval;
    inStr->ReadSegments(NS_DiscardSegment, nullptr, count, &_retval);
    return NS_OK;
  }

  return mDestListener->OnDataAvailable(aRequest, inStr, sourceOffset, count);
}

NS_IMETHODIMP
imgCacheValidator::OnDataFinished(nsresult aStatus) {
  if (!mDestListener) {
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<nsIThreadRetargetableStreamListener> retargetableListener =
      do_QueryInterface(mDestListener);
  if (retargetableListener) {
    return retargetableListener->OnDataFinished(aStatus);
  }

  return NS_OK;
}

/** nsIThreadRetargetableStreamListener methods **/

NS_IMETHODIMP
imgCacheValidator::CheckListenerChain() {
  NS_ASSERTION(NS_IsMainThread(), "Should be on the main thread!");
  nsresult rv = NS_OK;
  nsCOMPtr<nsIThreadRetargetableStreamListener> retargetableListener =
      do_QueryInterface(mDestListener, &rv);
  if (retargetableListener) {
    rv = retargetableListener->CheckListenerChain();
  }
  MOZ_LOG(
      gImgLog, LogLevel::Debug,
      ("[this=%p] imgCacheValidator::CheckListenerChain -- rv %" PRId32 "=%s",
       this, static_cast<uint32_t>(rv),
       NS_SUCCEEDED(rv) ? "succeeded" : "failed"));
  return rv;
}

/** nsIInterfaceRequestor methods **/

NS_IMETHODIMP
imgCacheValidator::GetInterface(const nsIID& aIID, void** aResult) {
  if (aIID.Equals(NS_GET_IID(nsIChannelEventSink))) {
    return QueryInterface(aIID, aResult);
  }

  return mProgressProxy->GetInterface(aIID, aResult);
}

// These functions are materially the same as the same functions in imgRequest.
// We duplicate them because we're verifying whether cache loads are necessary,
// not unconditionally loading.

/** nsIChannelEventSink methods **/
NS_IMETHODIMP
imgCacheValidator::AsyncOnChannelRedirect(
    nsIChannel* oldChannel, nsIChannel* newChannel, uint32_t flags,
    nsIAsyncVerifyRedirectCallback* callback) {
  // Note all cache information we get from the old channel.
  mNewRequest->SetCacheValidation(mNewEntry, oldChannel);

  // If the previous URI is a non-HTTPS URI, record that fact for later use by
  // security code, which needs to know whether there is an insecure load at any
  // point in the redirect chain.
  nsCOMPtr<nsIURI> oldURI;
  bool schemeLocal = false;
  if (NS_FAILED(oldChannel->GetURI(getter_AddRefs(oldURI))) ||
      NS_FAILED(NS_URIChainHasFlags(
          oldURI, nsIProtocolHandler::URI_IS_LOCAL_RESOURCE, &schemeLocal)) ||
      (!oldURI->SchemeIs("https") && !oldURI->SchemeIs("chrome") &&
       !schemeLocal)) {
    mHadInsecureRedirect = true;
  }

  // Prepare for callback
  mRedirectCallback = callback;
  mRedirectChannel = newChannel;

  return mProgressProxy->AsyncOnChannelRedirect(oldChannel, newChannel, flags,
                                                this);
}

NS_IMETHODIMP
imgCacheValidator::OnRedirectVerifyCallback(nsresult aResult) {
  // If we've already been told to abort, just do so.
  if (NS_FAILED(aResult)) {
    mRedirectCallback->OnRedirectVerifyCallback(aResult);
    mRedirectCallback = nullptr;
    mRedirectChannel = nullptr;
    return NS_OK;
  }

  // make sure we have a protocol that returns data rather than opens
  // an external application, e.g. mailto:
  nsCOMPtr<nsIURI> uri;
  mRedirectChannel->GetURI(getter_AddRefs(uri));

  nsresult result = NS_OK;

  if (nsContentUtils::IsExternalProtocol(uri)) {
    result = NS_ERROR_ABORT;
  }

  mRedirectCallback->OnRedirectVerifyCallback(result);
  mRedirectCallback = nullptr;
  mRedirectChannel = nullptr;
  return NS_OK;
}
