/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layout_InlineTranslator_h
#define mozilla_layout_InlineTranslator_h

#include <string>

#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Filters.h"
#include "mozilla/gfx/RecordedEvent.h"

namespace mozilla {
namespace gfx {

using gfx::DrawTarget;
using gfx::FilterNode;
using gfx::GradientStops;
using gfx::NativeFontResource;
using gfx::Path;
using gfx::ReferencePtr;
using gfx::ScaledFont;
using gfx::SourceSurface;
using gfx::Translator;

class InlineTranslator : public Translator {
 public:
  InlineTranslator();

  explicit InlineTranslator(DrawTarget* aDT, void* aFontContext = nullptr);

  bool TranslateRecording(char*, size_t len);

  void SetExternalSurfaces(
      nsRefPtrHashtable<nsUint64HashKey, SourceSurface>* aExternalSurfaces) {
    mExternalSurfaces = aExternalSurfaces;
  }
  void SetReferenceDrawTargetTransform(const Matrix& aTransform) {
    mBaseDTTransform = aTransform;
  }
  void SetBaseDrawTarget(DrawTarget* aDT) { mBaseDT = aDT; }

  /**
   * The objects known to the translator, which allow replaying the same
   * recording multiple times, if the recording refers to objects created by
   * previous recordings.
   */
  class State {
    friend class InlineTranslator;

    DrawTarget* mCurrentDT = nullptr;
    nsRefPtrHashtable<nsPtrHashKey<void>, DrawTarget> mDrawTargets;
    nsRefPtrHashtable<nsPtrHashKey<void>, Path> mPaths;
    nsRefPtrHashtable<nsPtrHashKey<void>, SourceSurface> mSourceSurfaces;
    nsRefPtrHashtable<nsPtrHashKey<void>, FilterNode> mFilterNodes;
    nsRefPtrHashtable<nsPtrHashKey<void>, GradientStops> mGradientStops;
    nsRefPtrHashtable<nsPtrHashKey<void>, ScaledFont> mScaledFonts;
    nsRefPtrHashtable<nsPtrHashKey<void>, UnscaledFont> mUnscaledFonts;
    nsRefPtrHashtable<nsUint64HashKey, NativeFontResource> mNativeFontResources;
  };
  void SaveState(State& aState) const;
  void RestoreState(const State& aState);

  DrawTarget* LookupDrawTarget(ReferencePtr aRefPtr) final {
    DrawTarget* result = mDrawTargets.GetWeak(aRefPtr);
    MOZ_ASSERT(result);
    return result;
  }

  Path* LookupPath(ReferencePtr aRefPtr) final {
    Path* result = mPaths.GetWeak(aRefPtr);
    MOZ_ASSERT(result);
    return result;
  }

  SourceSurface* LookupSourceSurface(ReferencePtr aRefPtr) final {
    SourceSurface* result = mSourceSurfaces.GetWeak(aRefPtr);
    MOZ_ASSERT(result);
    return result;
  }

  FilterNode* LookupFilterNode(ReferencePtr aRefPtr) final {
    FilterNode* result = mFilterNodes.GetWeak(aRefPtr);
    MOZ_ASSERT(result);
    return result;
  }

  already_AddRefed<GradientStops> LookupGradientStops(
      ReferencePtr aRefPtr) final {
    return mGradientStops.Get(aRefPtr);
  }

  ScaledFont* LookupScaledFont(ReferencePtr aRefPtr) final {
    ScaledFont* result = mScaledFonts.GetWeak(aRefPtr);
    MOZ_ASSERT(result);
    return result;
  }

  UnscaledFont* LookupUnscaledFont(ReferencePtr aRefPtr) final {
    UnscaledFont* result = mUnscaledFonts.GetWeak(aRefPtr);
    MOZ_ASSERT(result);
    return result;
  }

  NativeFontResource* LookupNativeFontResource(uint64_t aKey) final {
    NativeFontResource* result = mNativeFontResources.GetWeak(aKey);
    MOZ_ASSERT(result);
    return result;
  }

  already_AddRefed<SourceSurface> LookupExternalSurface(uint64_t aKey) override;

  void AddDrawTarget(ReferencePtr aRefPtr, DrawTarget* aDT) final {
    RefPtr<DrawTarget>& value = mDrawTargets.LookupOrInsert(aRefPtr);
    if (mCurrentDT && mCurrentDT == value) {
      mCurrentDT = nullptr;
    }
    value = aDT;
  }

  void AddPath(ReferencePtr aRefPtr, Path* aPath) final {
    mPaths.InsertOrUpdate(aRefPtr, RefPtr{aPath});
  }

  void AddSourceSurface(ReferencePtr aRefPtr,
                        SourceSurface* aSurface) override {
    mSourceSurfaces.InsertOrUpdate(aRefPtr, RefPtr{aSurface});
  }

  void AddFilterNode(ReferencePtr aRefPtr, FilterNode* aFilter) final {
    mFilterNodes.InsertOrUpdate(aRefPtr, RefPtr{aFilter});
  }

  void AddGradientStops(ReferencePtr aRefPtr, GradientStops* aStops) final {
    mGradientStops.InsertOrUpdate(aRefPtr, RefPtr{aStops});
  }

  void AddScaledFont(ReferencePtr aRefPtr, ScaledFont* aScaledFont) final {
    mScaledFonts.InsertOrUpdate(aRefPtr, RefPtr{aScaledFont});
  }

  void AddUnscaledFont(ReferencePtr aRefPtr,
                       UnscaledFont* aUnscaledFont) final {
    mUnscaledFonts.InsertOrUpdate(aRefPtr, RefPtr{aUnscaledFont});
  }

  void AddNativeFontResource(uint64_t aKey,
                             NativeFontResource* aScaledFontResouce) final {
    mNativeFontResources.InsertOrUpdate(aKey, RefPtr{aScaledFontResouce});
  }

  void RemoveDrawTarget(ReferencePtr aRefPtr) override {
    RefPtr<DrawTarget> removedDT;
    if (mDrawTargets.Remove(aRefPtr, getter_AddRefs(removedDT)) &&
        mCurrentDT == removedDT) {
      mCurrentDT = nullptr;
    }
  }

  bool SetCurrentDrawTarget(ReferencePtr aRefPtr) override {
    mCurrentDT = mDrawTargets.GetWeak(aRefPtr);
    return !!mCurrentDT;
  }

  void RemovePath(ReferencePtr aRefPtr) final { mPaths.Remove(aRefPtr); }

  void RemoveSourceSurface(ReferencePtr aRefPtr) override {
    mSourceSurfaces.Remove(aRefPtr);
  }

  void RemoveFilterNode(ReferencePtr aRefPtr) final {
    mFilterNodes.Remove(aRefPtr);
  }

  void RemoveGradientStops(ReferencePtr aRefPtr) final {
    mGradientStops.Remove(aRefPtr);
  }

  void RemoveScaledFont(ReferencePtr aRefPtr) final {
    mScaledFonts.Remove(aRefPtr);
  }

  void RemoveUnscaledFont(ReferencePtr aRefPtr) final {
    mUnscaledFonts.Remove(aRefPtr);
  }

  already_AddRefed<DrawTarget> CreateDrawTarget(
      ReferencePtr aRefPtr, const gfx::IntSize& aSize,
      gfx::SurfaceFormat aFormat) override;

  mozilla::gfx::DrawTarget* GetReferenceDrawTarget() final {
    MOZ_ASSERT(mBaseDT, "mBaseDT has not been initialized.");
    return mBaseDT;
  }
  Matrix GetReferenceDrawTargetTransform() final { return mBaseDTTransform; }

  void* GetFontContext() final { return mFontContext; }
  std::string GetError() { return mError; }

 protected:
  RefPtr<DrawTarget> mBaseDT;
  Matrix mBaseDTTransform;
  nsRefPtrHashtable<nsPtrHashKey<void>, DrawTarget> mDrawTargets;

 private:
  void* mFontContext;
  std::string mError;

  nsRefPtrHashtable<nsPtrHashKey<void>, Path> mPaths;
  nsRefPtrHashtable<nsPtrHashKey<void>, SourceSurface> mSourceSurfaces;
  nsRefPtrHashtable<nsPtrHashKey<void>, FilterNode> mFilterNodes;
  nsRefPtrHashtable<nsPtrHashKey<void>, GradientStops> mGradientStops;
  nsRefPtrHashtable<nsPtrHashKey<void>, ScaledFont> mScaledFonts;
  nsRefPtrHashtable<nsPtrHashKey<void>, UnscaledFont> mUnscaledFonts;
  nsRefPtrHashtable<nsUint64HashKey, NativeFontResource> mNativeFontResources;
  nsRefPtrHashtable<nsUint64HashKey, SourceSurface>* mExternalSurfaces =
      nullptr;
};

}  // namespace gfx
}  // namespace mozilla

#endif  // mozilla_layout_InlineTranslator_h
