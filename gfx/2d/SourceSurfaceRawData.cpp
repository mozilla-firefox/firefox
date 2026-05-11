/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SourceSurfaceRawData.h"

#include "DataSurfaceHelpers.h"
#include "Logging.h"
#include "nsIMemoryReporter.h"

namespace mozilla {
namespace gfx {

class SourceSurfaceAlignedRawDataReporter final : public nsIMemoryReporter {
  ~SourceSurfaceAlignedRawDataReporter() = default;

 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override {
    MOZ_COLLECT_REPORT("explicit/gfx/source-surface-aligned-raw-data",
                       KIND_HEAP, UNITS_BYTES, sTotalDataBytes,
                       "Total memory used by source surface aligned raw data.");
    return NS_OK;
  }

 private:
  friend class SourceSurfaceAlignedRawData;

  static Atomic<size_t> sTotalDataBytes;
};

NS_IMPL_ISUPPORTS(SourceSurfaceAlignedRawDataReporter, nsIMemoryReporter)

/* static */
Atomic<size_t> SourceSurfaceAlignedRawDataReporter::sTotalDataBytes(0);

/* static */
void SourceSurfaceAlignedRawData::RegisterMemoryReporter() {
  RegisterStrongMemoryReporter(
      MakeAndAddRef<SourceSurfaceAlignedRawDataReporter>());
}

void SourceSurfaceRawData::InitWrappingData(
    uint8_t* aData, const IntSize& aSize, int32_t aStride,
    SurfaceFormat aFormat, Factory::SourceSurfaceDeallocator aDeallocator,
    void* aClosure) {
  mRawData = aData;
  mSize = aSize;
  mStride = aStride;
  mFormat = aFormat;
  mDeallocator = aDeallocator;
  mClosure = aClosure;
}

void SourceSurfaceRawData::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf,
                                               SizeOfInfo& aInfo) const {
  aInfo.AddType(SurfaceType::DATA);
  if (mDeallocator) {
    aInfo.mUnknownBytes = mStride * mSize.height;
  }
}

SourceSurfaceAlignedRawData::SourceSurfaceAlignedRawData()
    : mStride(0), mFormat(SurfaceFormat::UNKNOWN) {}

SourceSurfaceAlignedRawData::~SourceSurfaceAlignedRawData() {
  size_t bufLen = BufferSizeFromStrideAndHeight(mStride, mSize.height);
  SourceSurfaceAlignedRawDataReporter::sTotalDataBytes -= bufLen;
}

bool SourceSurfaceAlignedRawData::Init(const IntSize& aSize,
                                       SurfaceFormat aFormat, bool aClearMem,
                                       uint8_t aClearValue, int32_t aStride) {
  mFormat = aFormat;
  mStride = aStride ? aStride
                    : GetAlignedStride<16>(aSize.width, BytesPerPixel(aFormat))
                          .valueOr(0);

  size_t bufLen =
      mStride ? BufferSizeFromStrideAndHeight(mStride, aSize.height) : 0;
  if (bufLen > 0) {
    bool zeroMem = aClearMem && !aClearValue;
    static_assert(sizeof(decltype(mArray[0])) == 1,
                  "mArray.Realloc() takes an object count, so its objects must "
                  "be 1-byte sized if we use bufLen");

    // AlignedArray uses cmalloc to zero mem for a fast path.
    mArray.Realloc(/* actually an object count */ bufLen, zeroMem);
    mSize = aSize;

    SourceSurfaceAlignedRawDataReporter::sTotalDataBytes += bufLen;

    if (mArray && aClearMem && aClearValue) {
      memset(mArray, aClearValue, mStride * aSize.height);
    }
  } else {
    mArray.Dealloc();
    mSize.SizeTo(0, 0);
  }

  return mArray != nullptr;
}

void SourceSurfaceAlignedRawData::SizeOfExcludingThis(
    MallocSizeOf aMallocSizeOf, SizeOfInfo& aInfo) const {
  aInfo.AddType(SurfaceType::DATA_ALIGNED);
  // This memory is accounted for in aggregate by sTotalDataBytes. We return
  // zero here to prevent double-counting.
  aInfo.mHeapBytes = 0;
}

}  // namespace gfx
}  // namespace mozilla
