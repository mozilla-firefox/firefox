/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_DRAWEVENTRECORDER_H_
#define MOZILLA_GFX_DRAWEVENTRECORDER_H_

#include "2D.h"
#include "RecordedEvent.h"
#include "RecordingTypes.h"

#include <deque>
#include <functional>
#include <vector>

#include "ImageContainer.h"
#include "mozilla/DataMutex.h"
#include "mozilla/ThreadSafeWeakPtr.h"
#include "nsTHashMap.h"
#include "nsTHashSet.h"
#include "nsISupportsImpl.h"

namespace mozilla {
namespace layers {
class CanvasChild;
}  // namespace layers

namespace gfx {

class DrawTargetRecording;
class PathRecording;

class DrawEventRecorderPrivate : public DrawEventRecorder {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(DrawEventRecorderPrivate, override)

  DrawEventRecorderPrivate();
  virtual ~DrawEventRecorderPrivate();
  RecorderType GetRecorderType() const override {
    return RecorderType::PRIVATE;
  }
  bool Finish() override {
    ClearResources();
    return true;
  }
  virtual void FlushItem(IntRect) {}
  virtual void DetachResources() {
    NS_ASSERT_OWNINGTHREAD(DrawEventRecorderPrivate);

    nsTHashSet<ScaledFont*> fonts = std::move(mStoredFonts);
    for (const auto& font : fonts) {
      font->RemoveUserData(reinterpret_cast<UserDataKey*>(this));
    }

    // SourceSurfaces can be deleted off the main thread, so we use
    // ThreadSafeWeakPtrs to allow for this. RemoveUserData is thread safe.
    nsTHashMap<void*, ThreadSafeWeakPtr<SourceSurface>> surfaces =
        std::move(mStoredSurfaces);
    for (const auto& entry : surfaces) {
      RefPtr<SourceSurface> strongRef(entry.GetData());
      if (strongRef) {
        strongRef->RemoveUserData(reinterpret_cast<UserDataKey*>(this));
      }
    }

    // Now that we've detached we can't get any more pending deletions, so
    // processing now should mean we include all clean up operations.
    ProcessPendingDeletions();
  }

  void ClearResources() {
    NS_ASSERT_OWNINGTHREAD(DrawEventRecorderPrivate);
    mStoredObjects.Clear();
    mStoredFontData.Clear();
    mScaledFonts.clear();
    mCurrentDT = nullptr;
  }

  template <class S>
  void WriteHeader(S& aStream) {
    NS_ASSERT_OWNINGTHREAD(DrawEventRecorderPrivate);
    WriteElement(aStream, kMagicInt);
    WriteElement(aStream, kMajorRevision);
    WriteElement(aStream, kMinorRevision);
  }

  virtual void RecordEvent(const RecordedEvent& aEvent) = 0;

  void RecordEvent(const DrawTargetRecording* aDT,
                   const RecordedEvent& aEvent) {
    ReferencePtr dt = aDT;
    if (mCurrentDT != dt) {
      SetDrawTarget(dt);
    }
    RecordEvent(aEvent);
  }

  void SetDrawTarget(ReferencePtr aDT);

  void ClearDrawTarget(const DrawTargetRecording* aDT) {
    ReferencePtr dt = aDT;
    if (mCurrentDT == dt) {
      mCurrentDT = nullptr;
    }
  }

  void AddStoredObject(const ReferencePtr aObject) {
    ProcessPendingDeletions();
    mStoredObjects.Insert(aObject);
  }

  /**
   * This is a combination of HasStoredObject and AddStoredObject, so that we
   * only have to call ProcessPendingDeletions once, which involves locking.
   * @param aObject the object to store if not already stored
   * @return true if the object was not already stored, false if it was
   */
  bool TryAddStoredObject(const ReferencePtr aObject) {
    ProcessPendingDeletions();
    return mStoredObjects.EnsureInserted(aObject);
  }

  virtual void AddPendingDeletion(std::function<void()>&& aPendingDeletion) {
    auto lockedPendingDeletions = mPendingDeletions.Lock();
    lockedPendingDeletions->emplace_back(std::move(aPendingDeletion));
  }

  void RemoveStoredObject(const ReferencePtr aObject) {
    NS_ASSERT_OWNINGTHREAD(DrawEventRecorderPrivate);
    mStoredObjects.Remove(aObject);
  }

  /**
   * @param aUnscaledFont the UnscaledFont to increment the reference count for
   * @return the previous reference count
   */
  int32_t IncrementUnscaledFontRefCount(const ReferencePtr aUnscaledFont) {
    NS_ASSERT_OWNINGTHREAD(DrawEventRecorderPrivate);
    int32_t& count = mUnscaledFontRefs.LookupOrInsert(aUnscaledFont, 0);
    return count++;
  }

  /**
   * Decrements the reference count for aUnscaledFont and, if count is now zero,
   * records its destruction.
   * @param aUnscaledFont the UnscaledFont to decrement the reference count for
   */
  void DecrementUnscaledFontRefCount(const ReferencePtr aUnscaledFont);

  void AddScaledFont(ScaledFont* aFont) {
    NS_ASSERT_OWNINGTHREAD(DrawEventRecorderPrivate);
    if (mStoredFonts.EnsureInserted(aFont) && WantsExternalFonts()) {
      mScaledFonts.push_back(aFont);
    }
  }

  void RemoveScaledFont(ScaledFont* aFont) { mStoredFonts.Remove(aFont); }

  void AddSourceSurface(SourceSurface* aSurface) {
    NS_ASSERT_OWNINGTHREAD(DrawEventRecorderPrivate);
    mStoredSurfaces.InsertOrUpdate(aSurface, aSurface);
  }

  void RemoveSourceSurface(SourceSurface* aSurface) {
    NS_ASSERT_OWNINGTHREAD(DrawEventRecorderPrivate);
    mStoredSurfaces.Remove(aSurface);
  }

#if defined(DEBUG)
  // Only used within debug assertions.
  bool HasStoredObject(const ReferencePtr aObject) {
    ProcessPendingDeletions();
    return mStoredObjects.Contains(aObject);
  }
#endif

  void AddStoredFontData(const uint64_t aFontDataKey) {
    NS_ASSERT_OWNINGTHREAD(DrawEventRecorderPrivate);
    mStoredFontData.Insert(aFontDataKey);
  }

  bool HasStoredFontData(const uint64_t aFontDataKey) {
    NS_ASSERT_OWNINGTHREAD(DrawEventRecorderPrivate);
    return mStoredFontData.Contains(aFontDataKey);
  }

  bool WantsExternalFonts() const { return mExternalFonts; }

  virtual void StoreSourceSurfaceRecording(SourceSurface* aSurface,
                                           const char* aReason);

  virtual void StoreImageRecording(
      const RefPtr<layers::Image>& aImageOfSurfaceDescriptor,
      const char* aReasony) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
  }

  /**
   * Used when a source surface is destroyed, aSurface is a void* instead of a
   * SourceSurface* because this is called during the SourceSurface destructor,
   * so it is partially destructed and should not be accessed.
   * @param aSurface the surface whose destruction is being recorded
   */
  void RecordSourceSurfaceDestruction(void* aSurface);

  virtual void AddDependentSurface(uint64_t aDependencyId) {
    MOZ_CRASH("GFX: AddDependentSurface");
  }

  struct ExternalSurfaceEntry {
    RefPtr<SourceSurface> mSurface;
    int64_t mEventCount = -1;
  };

  using ExternalSurfacesHolder = std::deque<ExternalSurfaceEntry>;

  void TakeExternalSurfaces(ExternalSurfacesHolder& aSurfaces) {
    NS_ASSERT_OWNINGTHREAD(DrawEventRecorderPrivate);
    aSurfaces = std::move(mExternalSurfaces);
  }

  struct ExternalImageEntry {
    RefPtr<layers::Image> mImage;
    int64_t mEventCount = -1;
  };

  using ExternalImagesHolder = std::deque<ExternalImageEntry>;

  virtual already_AddRefed<layers::CanvasChild> GetCanvasChild() const {
    return nullptr;
  }

 protected:
  NS_DECL_OWNINGTHREAD

  void StoreExternalSurfaceRecording(SourceSurface* aSurface, uint64_t aKey);

  void StoreExternalImageRecording(
      const RefPtr<layers::Image>& aImageOfSurfaceDescriptor);

  void ProcessPendingDeletions() {
    NS_ASSERT_OWNINGTHREAD(DrawEventRecorderPrivate);

    PendingDeletionsVector pendingDeletions;
    {
      auto lockedPendingDeletions = mPendingDeletions.Lock();
      pendingDeletions.swap(*lockedPendingDeletions);
    }
    for (const auto& pendingDeletion : pendingDeletions) {
      pendingDeletion();
    }
  }

  virtual void Flush() = 0;

  nsTHashSet<const void*> mStoredObjects;

  using PendingDeletionsVector = std::vector<std::function<void()>>;
  DataMutex<PendingDeletionsVector> mPendingDeletions{
      "DrawEventRecorderPrivate::mPendingDeletions"};

  // It's difficult to track the lifetimes of UnscaledFonts directly, so we
  // instead track the number of recorded ScaledFonts that hold a reference to
  // an Unscaled font and use that as a proxy to the real lifetime. An
  // UnscaledFonts lifetime could be longer than this, but we only use the
  // ScaledFonts directly and if another uses an UnscaledFont we have destroyed
  // on the translation side, it will be recreated.
  nsTHashMap<const void*, int32_t> mUnscaledFontRefs;

  nsTHashSet<uint64_t> mStoredFontData;
  nsTHashSet<ScaledFont*> mStoredFonts;
  std::vector<RefPtr<ScaledFont>> mScaledFonts;

  // SourceSurfaces can get deleted off the main thread, so we hold a map of the
  // raw pointer to a ThreadSafeWeakPtr to protect against this.
  nsTHashMap<void*, ThreadSafeWeakPtr<SourceSurface>> mStoredSurfaces;

  ReferencePtr mCurrentDT;
  ExternalSurfacesHolder mExternalSurfaces;
  ExternalImagesHolder mExternalImages;
  bool mExternalFonts;
};

typedef std::function<void(MemStream& aStream,
                           std::vector<RefPtr<ScaledFont>>& aScaledFonts)>
    SerializeResourcesFn;

// WARNING: This should not be used in its existing state because
// it is likely to OOM because of large continguous allocations.
class DrawEventRecorderMemory : public DrawEventRecorderPrivate {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(DrawEventRecorderMemory, override)

  /**
   * Constructs a DrawEventRecorder that stores the recording in memory.
   */
  DrawEventRecorderMemory();
  explicit DrawEventRecorderMemory(const SerializeResourcesFn& aSerialize);

  RecorderType GetRecorderType() const override { return RecorderType::MEMORY; }

  void RecordEvent(const RecordedEvent& aEvent) override;

  void AddDependentSurface(uint64_t aDependencyId) override;

  nsTHashSet<uint64_t>&& TakeDependentSurfaces();

  /**
   * @return the current size of the recording (in chars).
   */
  size_t RecordingSize();

  /**
   * Wipes the internal recording buffer, but the recorder does NOT forget which
   * objects it has recorded. This can be used so that a recording can be copied
   * and processed in chunks, releasing memory as it goes.
   */
  void WipeRecording();
  bool Finish() override;
  void FlushItem(IntRect) override;

  MemStream mOutputStream;
  /* The index stream is of the form:
   * ItemIndex { size_t dataEnd; size_t extraDataEnd; }
   * It gets concatenated to the end of mOutputStream in Finish()
   * The last size_t in the stream is offset of the begining of the
   * index.
   */
  MemStream mIndex;

 protected:
  virtual ~DrawEventRecorderMemory() = default;

 private:
  SerializeResourcesFn mSerializeCallback;
  nsTHashSet<uint64_t> mDependentSurfaces;

  void Flush() override;
};

}  // namespace gfx
}  // namespace mozilla

#endif /* MOZILLA_GFX_DRAWEVENTRECORDER_H_ */
