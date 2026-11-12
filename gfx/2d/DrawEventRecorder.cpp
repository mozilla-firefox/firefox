/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DrawEventRecorder.h"

#include "PathRecording.h"
#include "RecordedEventImpl.h"
#include "RecordingTypes.h"

namespace mozilla {
namespace gfx {

DrawEventRecorderPrivate::DrawEventRecorderPrivate() : mExternalFonts(false) {}

DrawEventRecorderPrivate::~DrawEventRecorderPrivate() = default;

void DrawEventRecorderPrivate::SetDrawTarget(ReferencePtr aDT) {
  NS_ASSERT_OWNINGTHREAD(DrawEventRecorderPrivate);

  RecordEvent(RecordedSetCurrentDrawTarget(aDT));
  mCurrentDT = aDT;
}

void DrawEventRecorderPrivate::StoreExternalSurfaceRecording(
    SourceSurface* aSurface, uint64_t aKey) {
  NS_ASSERT_OWNINGTHREAD(DrawEventRecorderPrivate);

  RecordEvent(RecordedExternalSurfaceCreation(aSurface, aKey));
  mExternalSurfaces.push_back({aSurface});
}

void DrawEventRecorderPrivate::StoreExternalImageRecording(
    const RefPtr<layers::Image>& aImageOfSurfaceDescriptor) {
  NS_ASSERT_OWNINGTHREAD(DrawEventRecorderPrivate);

  mExternalImages.push_back({aImageOfSurfaceDescriptor});
}

void DrawEventRecorderPrivate::StoreSourceSurfaceRecording(
    SourceSurface* aSurface, const char* aReason) {
  NS_ASSERT_OWNINGTHREAD(DrawEventRecorderPrivate);

  RefPtr<DataSourceSurface> dataSurf;
  IntSize surfaceSize = aSurface->GetSize();
  if (Factory::AllowedSurfaceSize(surfaceSize)) {
    dataSurf = aSurface->GetDataSurface();
  }
  if (!dataSurf) {
    gfxWarning() << "Recording failed to record SourceSurface for " << aReason;

    // If surface size is not allowed, replace with reasonable size.
    surfaceSize.width = std::min(surfaceSize.width, kReasonableSurfaceSize);
    surfaceSize.height = std::min(surfaceSize.height, kReasonableSurfaceSize);

    // Insert a dummy source surface.
    if (Factory::AllowedSurfaceSize(surfaceSize)) {
      dataSurf = Factory::CreateDataSourceSurface(surfaceSize,
                                                  aSurface->GetFormat(), true);
    }
    if (!dataSurf) {
      // If the surface is too big just create a 1 x 1 dummy.
      dataSurf = Factory::CreateDataSourceSurface(IntSize(1, 1),
                                                  aSurface->GetFormat(), true);
      if (!dataSurf) {
        return;
      }
    }
  }

  RecordEvent(RecordedSourceSurfaceCreation(aSurface, dataSurf));
}

void DrawEventRecorderPrivate::RecordSourceSurfaceDestruction(void* aSurface) {
  NS_ASSERT_OWNINGTHREAD(DrawEventRecorderPrivate);

  RemoveSourceSurface(static_cast<SourceSurface*>(aSurface));
  RemoveStoredObject(aSurface);
  RecordEvent(RecordedSourceSurfaceDestruction(ReferencePtr(aSurface)));
}

void DrawEventRecorderPrivate::DecrementUnscaledFontRefCount(
    const ReferencePtr aUnscaledFont) {
  NS_ASSERT_OWNINGTHREAD(DrawEventRecorderPrivate);

  auto element = mUnscaledFontRefs.Lookup(aUnscaledFont);
  MOZ_DIAGNOSTIC_ASSERT(element,
                        "DecrementUnscaledFontRefCount calls should balance "
                        "with IncrementUnscaledFontRefCount calls");
  if (--element.Data() <= 0) {
    RecordEvent(RecordedUnscaledFontDestruction(aUnscaledFont));
    element.Remove();
  }
}

void DrawEventRecorderMemory::RecordEvent(const RecordedEvent& aEvent) {
  NS_ASSERT_OWNINGTHREAD(DrawEventRecorderMemory);
  aEvent.RecordToStream(mOutputStream);
}

void DrawEventRecorderMemory::AddDependentSurface(uint64_t aDependencyId) {
  NS_ASSERT_OWNINGTHREAD(DrawEventRecorderMemory);
  mDependentSurfaces.Insert(aDependencyId);
}

nsTHashSet<uint64_t>&& DrawEventRecorderMemory::TakeDependentSurfaces() {
  NS_ASSERT_OWNINGTHREAD(DrawEventRecorderMemory);
  return std::move(mDependentSurfaces);
}

DrawEventRecorderMemory::DrawEventRecorderMemory() {
  WriteHeader(mOutputStream);
}

DrawEventRecorderMemory::DrawEventRecorderMemory(
    const SerializeResourcesFn& aFn)
    : mSerializeCallback(aFn) {
  mExternalFonts = !!mSerializeCallback;
  WriteHeader(mOutputStream);
}

void DrawEventRecorderMemory::Flush() {
  NS_ASSERT_OWNINGTHREAD(DrawEventRecorderMemory);
}

void DrawEventRecorderMemory::FlushItem(IntRect aRect) {
  NS_ASSERT_OWNINGTHREAD(DrawEventRecorderMemory);

  MOZ_RELEASE_ASSERT(!aRect.IsEmpty());
  // Detaching our existing resources will add some
  // destruction events to our stream so we need to do that
  // first.
  DetachResources();

  // See moz2d_renderer.rs for a description of the stream format
  WriteElement(mIndex, mOutputStream.mLength);

  // write out the fonts into the extra data section
  mSerializeCallback(mOutputStream, mScaledFonts);
  WriteElement(mIndex, mOutputStream.mLength);

  WriteElement(mIndex, aRect.x);
  WriteElement(mIndex, aRect.y);
  WriteElement(mIndex, aRect.XMost());
  WriteElement(mIndex, aRect.YMost());
  ClearResources();

  // write out a new header for the next recording in the stream
  WriteHeader(mOutputStream);
}

bool DrawEventRecorderMemory::Finish() {
  NS_ASSERT_OWNINGTHREAD(DrawEventRecorderMemory);

  // this length might be 0, and things should still work.
  // for example if there are no items in a particular area
  size_t indexOffset = mOutputStream.mLength;
  // write out the index
  mOutputStream.write(mIndex.mData, mIndex.mLength);
  bool hasItems = mIndex.mLength != 0;
  mIndex.reset();
  // write out the offset of the Index to the end of the output stream
  WriteElement(mOutputStream, indexOffset);
  ClearResources();
  return hasItems;
}

size_t DrawEventRecorderMemory::RecordingSize() {
  NS_ASSERT_OWNINGTHREAD(DrawEventRecorderMemory);
  return mOutputStream.mLength;
}

void DrawEventRecorderMemory::UseDeterministicReferenceIds() {
  NS_ASSERT_OWNINGTHREAD(DrawEventRecorderMemory);
  if (!mReferenceIds) {
    mReferenceIds.reset(new ReferenceIdMap());
    mOutputStream.mReferenceIds = mReferenceIds.get();
  }
}

void DrawEventRecorderMemory::WipeRecording() {
  NS_ASSERT_OWNINGTHREAD(DrawEventRecorderMemory);

  mOutputStream.reset();
  mIndex.reset();
  if (mReferenceIds) {
    mReferenceIds->Clear();
  }

  WriteHeader(mOutputStream);
}

}  // namespace gfx
}  // namespace mozilla
