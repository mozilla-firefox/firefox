/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DrawTargetRecording.h"
#include "DrawTargetSkia.h"
#include "PathRecording.h"
#include <stdio.h>

#include "ImageContainer.h"
#include "Logging.h"
#include "Tools.h"
#include "Filters.h"
#include "mozilla/gfx/DataSurfaceHelpers.h"
#include "mozilla/layers/CanvasDrawEventRecorder.h"
#include "mozilla/layers/RecordedCanvasEventImpl.h"
#include "mozilla/layers/SourceSurfaceSharedData.h"
#include "mozilla/layers/TextureRecorded.h"
#include "mozilla/UniquePtr.h"
#include "nsXULAppAPI.h"  // for XRE_IsContentProcess()
#include "RecordingTypes.h"
#include "RecordedEventImpl.h"

namespace mozilla {
namespace gfx {

struct RecordingSourceSurfaceUserData {
  void* refPtr;
  RefPtr<DrawEventRecorderPrivate> recorder;

  // The optimized surface holds a reference to our surface, for GetDataSurface
  // calls, so we must hold a weak reference to avoid circular dependency.
  ThreadSafeWeakPtr<SourceSurface> optimizedSurface;
};

static void RecordingSourceSurfaceUserDataFunc(void* aUserData) {
  RecordingSourceSurfaceUserData* userData =
      static_cast<RecordingSourceSurfaceUserData*>(aUserData);

  if (NS_IsMainThread()) {
    userData->recorder->RecordSourceSurfaceDestruction(userData->refPtr);
    delete userData;
    return;
  }

  userData->recorder->AddPendingDeletion([userData]() -> void {
    userData->recorder->RecordSourceSurfaceDestruction(userData->refPtr);
    delete userData;
  });
}

static bool EnsureSurfaceStoredRecording(DrawEventRecorderPrivate* aRecorder,
                                         SourceSurface* aSurface,
                                         const char* reason) {
  // It's important that TryAddStoredObject is called first because that will
  // run any pending processing required by recorded objects that have been
  // deleted off the main thread.
  if (!aRecorder->TryAddStoredObject(aSurface)) {
    // Surface is already stored.
    return false;
  }
  aRecorder->StoreSourceSurfaceRecording(aSurface, reason);
  aRecorder->AddSourceSurface(aSurface);

  RecordingSourceSurfaceUserData* userData = new RecordingSourceSurfaceUserData;
  userData->refPtr = aSurface;
  userData->recorder = aRecorder;
  aSurface->AddUserData(reinterpret_cast<UserDataKey*>(aRecorder), userData,
                        &RecordingSourceSurfaceUserDataFunc);
  return true;
}

class SourceSurfaceRecording : public SourceSurface {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(SourceSurfaceRecording, override)

  SourceSurfaceRecording(IntSize aSize, SurfaceFormat aFormat,
                         DrawEventRecorderPrivate* aRecorder,
                         SourceSurface* aOriginalSurface = nullptr)
      : mSize(aSize),
        mFormat(aFormat),
        mRecorder(aRecorder),
        mOriginalSurface(aOriginalSurface) {
    mRecorder->AddStoredObject(this);
  }

  ~SourceSurfaceRecording() {
    mRecorder->RemoveStoredObject(this);
    mRecorder->RecordEvent(
        RecordedSourceSurfaceDestruction(ReferencePtr(this)));
  }

  SurfaceType GetType() const override { return SurfaceType::RECORDING; }
  IntSize GetSize() const override { return mSize; }
  SurfaceFormat GetFormat() const override { return mFormat; }
  already_AddRefed<DataSourceSurface> GetDataSurface() override {
    if (mOriginalSurface) {
      return mOriginalSurface->GetDataSurface();
    }

    return nullptr;
  }

  already_AddRefed<SourceSurface> ExtractSubrect(const IntRect& aRect) override;

  IntSize mSize;
  SurfaceFormat mFormat;
  RefPtr<DrawEventRecorderPrivate> mRecorder;
  // If a SourceSurfaceRecording is returned from an OptimizeSourceSurface call
  // we need GetDataSurface to work, so we hold the original surface we
  // optimized to return its GetDataSurface.
  RefPtr<SourceSurface> mOriginalSurface;
};

class GradientStopsRecording : public GradientStops {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(GradientStopsRecording, override)

  explicit GradientStopsRecording(DrawEventRecorderPrivate* aRecorder)
      : mRecorder(aRecorder) {
    mRecorder->AddStoredObject(this);
  }

  virtual ~GradientStopsRecording() {
    mRecorder->RemoveStoredObject(this);
    mRecorder->RecordEvent(
        RecordedGradientStopsDestruction(ReferencePtr(this)));
  }

  BackendType GetBackendType() const override { return BackendType::RECORDING; }

  RefPtr<DrawEventRecorderPrivate> mRecorder;
};

class FilterNodeRecording : public FilterNode {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(FilterNodeRecording, override)
  using FilterNode::SetAttribute;

  explicit FilterNodeRecording(DrawEventRecorderPrivate* aRecorder)
      : mRecorder(aRecorder) {
    mRecorder->AddStoredObject(this);
  }

  virtual ~FilterNodeRecording() {
    mRecorder->RemoveStoredObject(this);
    mRecorder->RecordEvent(RecordedFilterNodeDestruction(ReferencePtr(this)));
  }

  void SetInput(uint32_t aIndex, SourceSurface* aSurface) override {
    EnsureSurfaceStoredRecording(mRecorder, aSurface, "SetInput");

    mRecorder->RecordEvent(RecordedFilterNodeSetInput(this, aIndex, aSurface));
  }
  void SetInput(uint32_t aIndex, FilterNode* aFilter) override {
    MOZ_ASSERT(mRecorder->HasStoredObject(aFilter));

    mRecorder->RecordEvent(RecordedFilterNodeSetInput(this, aIndex, aFilter));
  }

#define FORWARD_SET_ATTRIBUTE(type, argtype)                 \
  void SetAttribute(uint32_t aIndex, type aValue) override { \
    mRecorder->RecordEvent(RecordedFilterNodeSetAttribute(   \
        this, aIndex, aValue,                                \
        RecordedFilterNodeSetAttribute::ARGTYPE_##argtype)); \
  }

  FORWARD_SET_ATTRIBUTE(bool, BOOL);
  FORWARD_SET_ATTRIBUTE(uint32_t, UINT32);
  FORWARD_SET_ATTRIBUTE(Float, FLOAT);
  FORWARD_SET_ATTRIBUTE(const Size&, SIZE);
  FORWARD_SET_ATTRIBUTE(const IntSize&, INTSIZE);
  FORWARD_SET_ATTRIBUTE(const IntPoint&, INTPOINT);
  FORWARD_SET_ATTRIBUTE(const Rect&, RECT);
  FORWARD_SET_ATTRIBUTE(const IntRect&, INTRECT);
  FORWARD_SET_ATTRIBUTE(const Point&, POINT);
  FORWARD_SET_ATTRIBUTE(const Matrix&, MATRIX);
  FORWARD_SET_ATTRIBUTE(const Matrix5x4&, MATRIX5X4);
  FORWARD_SET_ATTRIBUTE(const Point3D&, POINT3D);
  FORWARD_SET_ATTRIBUTE(const DeviceColor&, COLOR);

#undef FORWARD_SET_ATTRIBUTE

  void SetAttribute(uint32_t aIndex, const Float* aFloat,
                    uint32_t aSize) override {
    mRecorder->RecordEvent(
        RecordedFilterNodeSetAttribute(this, aIndex, aFloat, aSize));
  }

  FilterBackend GetBackendType() override { return FILTER_BACKEND_RECORDING; }

  RefPtr<DrawEventRecorderPrivate> mRecorder;
};

DrawTargetRecording::DrawTargetRecording(
    layers::CanvasDrawEventRecorder* aRecorder,
    const layers::RemoteTextureOwnerId& aTextureOwnerId, DrawTarget* aDT,
    const IntSize& aSize)
    : mRecorder(static_cast<DrawEventRecorderPrivate*>(aRecorder)),
      mFinalDT(aDT),
      mRect(IntPoint(0, 0), aSize) {
  RecordEventSkipFlushTransform(layers::RecordedCanvasDrawTargetCreation(
      this, aTextureOwnerId, mFinalDT->GetBackendType(), aSize,
      mFinalDT->GetFormat()));
  mFormat = mFinalDT->GetFormat();
  DrawTarget::SetPermitSubpixelAA(IsOpaque(mFormat));
}

DrawTargetRecording::DrawTargetRecording(DrawEventRecorder* aRecorder,
                                         DrawTarget* aDT, IntRect aRect,
                                         bool aHasData)
    : mRecorder(static_cast<DrawEventRecorderPrivate*>(aRecorder)),
      mFinalDT(aDT),
      mRect(aRect) {
  MOZ_DIAGNOSTIC_ASSERT(aRecorder->GetRecorderType() != RecorderType::CANVAS);
  RefPtr<SourceSurface> snapshot = aHasData ? mFinalDT->Snapshot() : nullptr;
  RecordEventSkipFlushTransform(
      RecordedDrawTargetCreation(this, mFinalDT->GetBackendType(), mRect,
                                 mFinalDT->GetFormat(), aHasData, snapshot));
  mFormat = mFinalDT->GetFormat();
  DrawTarget::SetPermitSubpixelAA(IsOpaque(mFormat));
}

DrawTargetRecording::DrawTargetRecording(const DrawTargetRecording* aDT,
                                         IntRect aRect, SurfaceFormat aFormat)
    : mRecorder(aDT->mRecorder), mFinalDT(aDT->mFinalDT), mRect(aRect) {
  mFormat = aFormat;
  DrawTarget::SetPermitSubpixelAA(IsOpaque(mFormat));
}

DrawTargetRecording::~DrawTargetRecording() {
  RecordEventSkipFlushTransform(
      RecordedDrawTargetDestruction(ReferencePtr(this)));
  mRecorder->ClearDrawTarget(this);
}

void DrawTargetRecording::Link(const char* aLocalDest, const char* aURI,
                               const Rect& aRect) {
  MarkChanged();

  RecordEventSelf(RecordedLink(aLocalDest, aURI, aRect));
}

void DrawTargetRecording::Destination(const char* aDestination,
                                      const Point& aPoint) {
  MarkChanged();

  RecordEventSelf(RecordedDestination(aDestination, aPoint));
}

void DrawTargetRecording::FillRect(const Rect& aRect, const Pattern& aPattern,
                                   const DrawOptions& aOptions) {
  MarkChanged();

  EnsurePatternDependenciesStored(aPattern);

  RecordEventSelf(RecordedFillRect(aRect, aPattern, aOptions));
}

void DrawTargetRecording::StrokeRect(const Rect& aRect, const Pattern& aPattern,
                                     const StrokeOptions& aStrokeOptions,
                                     const DrawOptions& aOptions) {
  MarkChanged();

  EnsurePatternDependenciesStored(aPattern);

  RecordEventSelf(
      RecordedStrokeRect(aRect, aPattern, aStrokeOptions, aOptions));
}

void DrawTargetRecording::StrokeLine(const Point& aBegin, const Point& aEnd,
                                     const Pattern& aPattern,
                                     const StrokeOptions& aStrokeOptions,
                                     const DrawOptions& aOptions) {
  MarkChanged();

  EnsurePatternDependenciesStored(aPattern);

  RecordEventSelf(
      RecordedStrokeLine(aBegin, aEnd, aPattern, aStrokeOptions, aOptions));
}

void DrawTargetRecording::Fill(const Path* aPath, const Pattern& aPattern,
                               const DrawOptions& aOptions) {
  if (!aPath) {
    return;
  }

  MarkChanged();

  if (aPath->GetBackendType() == BackendType::RECORDING) {
    const PathRecording* path = static_cast<const PathRecording*>(aPath);
    auto circle = path->AsCircle();
    if (circle) {
      EnsurePatternDependenciesStored(aPattern);
      RecordEventSelf(RecordedFillCircle(circle.value(), aPattern, aOptions));
      return;
    }
  }

  RefPtr<PathRecording> pathRecording = EnsurePathStored(aPath);
  EnsurePatternDependenciesStored(aPattern);
  RecordEventSelf(RecordedFill(pathRecording, aPattern, aOptions));
}

struct RecordingFontUserData {
  void* refPtr;
  void* unscaledFont;
  RefPtr<DrawEventRecorderPrivate> recorder;
};

static void RecordingFontUserDataDestroyFunc(void* aUserData) {
  RecordingFontUserData* userData =
      static_cast<RecordingFontUserData*>(aUserData);

  userData->recorder->RecordEvent(
      RecordedScaledFontDestruction(ReferencePtr(userData->refPtr)));
  userData->recorder->RemoveScaledFont((ScaledFont*)userData->refPtr);
  userData->recorder->DecrementUnscaledFontRefCount(userData->unscaledFont);
  delete userData;
}

void DrawTargetRecording::DrawGlyphs(ScaledFont* aFont,
                                     const GlyphBuffer& aBuffer,
                                     const Pattern& aPattern,
                                     const DrawOptions& aOptions,
                                     const StrokeOptions* aStrokeOptions) {
  if (!aFont) {
    return;
  }

  MarkChanged();

  EnsurePatternDependenciesStored(aPattern);

  UserDataKey* userDataKey = reinterpret_cast<UserDataKey*>(mRecorder.get());
  if (mRecorder->WantsExternalFonts()) {
    mRecorder->AddScaledFont(aFont);
  } else if (!aFont->GetUserData(userDataKey)) {
    UnscaledFont* unscaledFont = aFont->GetUnscaledFont();
    if (mRecorder->IncrementUnscaledFontRefCount(unscaledFont) == 0) {
      // Prefer sending the description, if we can create one. This ensures
      // we don't record the data of system fonts which saves time and can
      // prevent duplicate copies from accumulating in the OS cache during
      // playback.
      RecordedFontDescriptor fontDesc(unscaledFont);
      if (fontDesc.IsValid()) {
        RecordEventSkipFlushTransform(fontDesc);
      } else {
        RecordedFontData fontData(unscaledFont);
        RecordedFontDetails fontDetails;
        if (fontData.GetFontDetails(fontDetails)) {
          // Try to serialise the whole font, just in case this is a web font
          // that is not present on the system.
          if (!mRecorder->HasStoredFontData(fontDetails.fontDataKey)) {
            RecordEventSkipFlushTransform(fontData);
            mRecorder->AddStoredFontData(fontDetails.fontDataKey);
          }
          RecordEventSkipFlushTransform(
              RecordedUnscaledFontCreation(unscaledFont, fontDetails));
        } else {
          gfxWarning() << "DrawTargetRecording::FillGlyphs failed to serialise "
                          "UnscaledFont";
        }
      }
    }
    RecordEventSkipFlushTransform(
        RecordedScaledFontCreation(aFont, unscaledFont));
    RecordingFontUserData* userData = new RecordingFontUserData;
    userData->refPtr = aFont;
    userData->unscaledFont = unscaledFont;
    userData->recorder = mRecorder;
    aFont->AddUserData(userDataKey, userData,
                       &RecordingFontUserDataDestroyFunc);
    userData->recorder->AddScaledFont(aFont);
  }

  if (aStrokeOptions) {
    RecordEventSelf(RecordedStrokeGlyphs(aFont, aPattern, *aStrokeOptions,
                                         aOptions, aBuffer.mGlyphs,
                                         aBuffer.mNumGlyphs));
  } else {
    RecordEventSelf(RecordedFillGlyphs(aFont, aPattern, aOptions,
                                       aBuffer.mGlyphs, aBuffer.mNumGlyphs));
  }
}

void DrawTargetRecording::FillGlyphs(ScaledFont* aFont,
                                     const GlyphBuffer& aBuffer,
                                     const Pattern& aPattern,
                                     const DrawOptions& aOptions) {
  DrawGlyphs(aFont, aBuffer, aPattern, aOptions);
}

void DrawTargetRecording::StrokeGlyphs(ScaledFont* aFont,
                                       const GlyphBuffer& aBuffer,
                                       const Pattern& aPattern,
                                       const StrokeOptions& aStrokeOptions,
                                       const DrawOptions& aOptions) {
  DrawGlyphs(aFont, aBuffer, aPattern, aOptions, &aStrokeOptions);
}

void DrawTargetRecording::Mask(const Pattern& aSource, const Pattern& aMask,
                               const DrawOptions& aOptions) {
  MarkChanged();

  EnsurePatternDependenciesStored(aSource);
  EnsurePatternDependenciesStored(aMask);

  RecordEventSelf(RecordedMask(aSource, aMask, aOptions));
}

void DrawTargetRecording::MaskSurface(const Pattern& aSource,
                                      SourceSurface* aMask, Point aOffset,
                                      const DrawOptions& aOptions) {
  if (!aMask) {
    return;
  }

  MarkChanged();

  EnsurePatternDependenciesStored(aSource);
  EnsureSurfaceStoredRecording(mRecorder, aMask, "MaskSurface");

  RecordEventSelf(RecordedMaskSurface(aSource, aMask, aOffset, aOptions));
}

void DrawTargetRecording::Stroke(const Path* aPath, const Pattern& aPattern,
                                 const StrokeOptions& aStrokeOptions,
                                 const DrawOptions& aOptions) {
  MarkChanged();

  if (aPath->GetBackendType() == BackendType::RECORDING) {
    const PathRecording* path = static_cast<const PathRecording*>(aPath);
    auto circle = path->AsCircle();
    if (circle && circle->closed) {
      EnsurePatternDependenciesStored(aPattern);
      RecordEventSelf(RecordedStrokeCircle(circle.value(), aPattern,
                                           aStrokeOptions, aOptions));
      return;
    }

    auto line = path->AsLine();
    if (line) {
      EnsurePatternDependenciesStored(aPattern);
      RecordEventSelf(RecordedStrokeLine(line->origin, line->destination,
                                         aPattern, aStrokeOptions, aOptions));
      return;
    }
  }

  RefPtr<PathRecording> pathRecording = EnsurePathStored(aPath);
  EnsurePatternDependenciesStored(aPattern);

  RecordEventSelf(
      RecordedStroke(pathRecording, aPattern, aStrokeOptions, aOptions));
}

void DrawTargetRecording::DrawShadow(const Path* aPath, const Pattern& aPattern,
                                     const ShadowOptions& aShadow,
                                     const DrawOptions& aOptions,
                                     const StrokeOptions* aStrokeOptions) {
  MarkChanged();

  RefPtr<PathRecording> pathRecording = EnsurePathStored(aPath);
  EnsurePatternDependenciesStored(aPattern);

  RecordEventSelf(RecordedDrawShadow(pathRecording, aPattern, aShadow, aOptions,
                                     aStrokeOptions));
}

void DrawTargetRecording::MarkChanged() {
  if (mTextureData) {
    mTextureData->DrawTargetWillChange();
  }
}

already_AddRefed<SourceSurface> DrawTargetRecording::Snapshot() {
  RefPtr<SourceSurface> retSurf =
      new SourceSurfaceRecording(mRect.Size(), mFormat, mRecorder);

  RecordEventSelfSkipFlushTransform(RecordedSnapshot(ReferencePtr(retSurf)));

  return retSurf.forget();
}

already_AddRefed<SourceSurface>
DrawTargetRecording::CreateExternalSourceSurface(const IntSize& aSize,
                                                 SurfaceFormat aFormat) {
  RefPtr<SourceSurface> retSurf =
      new SourceSurfaceRecording(aSize, aFormat, mRecorder);

  return retSurf.forget();
}

already_AddRefed<SourceSurface> DrawTargetRecording::SnapshotExternalCanvas(
    nsICanvasRenderingContextInternal* aCanvas,
    mozilla::ipc::IProtocol* aActor) {
  if (RefPtr<layers::CanvasChild> canvasChild = mRecorder->GetCanvasChild()) {
    return canvasChild->SnapshotExternalCanvas(this, aCanvas, aActor);
  }
  return nullptr;
}

already_AddRefed<SourceSurface> DrawTargetRecording::IntoLuminanceSource(
    LuminanceType aLuminanceType, float aOpacity) {
  RefPtr<SourceSurface> retSurf =
      new SourceSurfaceRecording(mRect.Size(), SurfaceFormat::A8, mRecorder);

  RecordEventSelfSkipFlushTransform(
      RecordedIntoLuminanceSource(retSurf, aLuminanceType, aOpacity));

  return retSurf.forget();
}

already_AddRefed<SourceSurface> SourceSurfaceRecording::ExtractSubrect(
    const IntRect& aRect) {
  if (aRect.IsEmpty() || !GetRect().Contains(aRect)) {
    return nullptr;
  }

  RefPtr<SourceSurface> subSurf =
      new SourceSurfaceRecording(aRect.Size(), mFormat, mRecorder);
  mRecorder->RecordEvent(RecordedExtractSubrect(subSurf, this, aRect));
  return subSurf.forget();
}

void DrawTargetRecording::Flush() {
  RecordEventSelfSkipFlushTransform(RecordedFlush());
}

void DrawTargetRecording::DetachAllSnapshots() {
  RecordEventSelfSkipFlushTransform(RecordedDetachAllSnapshots());
}

void DrawTargetRecording::DrawSurface(SourceSurface* aSurface,
                                      const Rect& aDest, const Rect& aSource,
                                      const DrawSurfaceOptions& aSurfOptions,
                                      const DrawOptions& aOptions) {
  if (!aSurface) {
    return;
  }

  MarkChanged();

  EnsureSurfaceStoredRecording(mRecorder, aSurface, "DrawSurface");

  RecordEventSelf(
      RecordedDrawSurface(aSurface, aDest, aSource, aSurfOptions, aOptions));
}

void DrawTargetRecording::DrawSurfaceDescriptor(
    const layers::SurfaceDescriptor& aDesc,
    const RefPtr<layers::Image>& aImageOfSurfaceDescriptor, const Rect& aDest,
    const Rect& aSource, const DrawSurfaceOptions& aSurfOptions,
    const DrawOptions& aOptions) {
  MarkChanged();

  mRecorder->StoreImageRecording(aImageOfSurfaceDescriptor,
                                 "DrawSurfaceDescriptor");

  RecordEventSelf(RecordedDrawSurfaceDescriptor(aDesc, aDest, aSource,
                                                aSurfOptions, aOptions));
}

void DrawTargetRecording::DrawDependentSurface(uint64_t aId,
                                               const Rect& aDest) {
  MarkChanged();

  mRecorder->AddDependentSurface(aId);
  RecordEventSelf(RecordedDrawDependentSurface(aId, aDest));
}

void DrawTargetRecording::DrawSurfaceWithShadow(SourceSurface* aSurface,
                                                const Point& aDest,
                                                const ShadowOptions& aShadow,
                                                CompositionOp aOp) {
  if (!aSurface) {
    return;
  }

  MarkChanged();

  EnsureSurfaceStoredRecording(mRecorder, aSurface, "DrawSurfaceWithShadow");

  RecordEventSelf(RecordedDrawSurfaceWithShadow(aSurface, aDest, aShadow, aOp));
}

void DrawTargetRecording::DrawFilter(FilterNode* aNode, const Rect& aSourceRect,
                                     const Point& aDestPoint,
                                     const DrawOptions& aOptions) {
  if (!aNode) {
    return;
  }

  MarkChanged();

  MOZ_ASSERT(mRecorder->HasStoredObject(aNode));

  RecordEventSelf(RecordedDrawFilter(aNode, aSourceRect, aDestPoint, aOptions));
}

already_AddRefed<FilterNode> DrawTargetRecording::CreateFilter(
    FilterType aType) {
  RefPtr<FilterNode> retNode = new FilterNodeRecording(mRecorder);

  RecordEventSelfSkipFlushTransform(RecordedFilterNodeCreation(retNode, aType));

  return retNode.forget();
}

void DrawTargetRecording::ClearRect(const Rect& aRect) {
  MarkChanged();

  RecordEventSelf(RecordedClearRect(aRect));
}

void DrawTargetRecording::CopySurface(SourceSurface* aSurface,
                                      const IntRect& aSourceRect,
                                      const IntPoint& aDestination) {
  if (!aSurface) {
    return;
  }

  MarkChanged();

  EnsureSurfaceStoredRecording(mRecorder, aSurface, "CopySurface");

  RecordEventSelf(RecordedCopySurface(aSurface, aSourceRect, aDestination));
}

void DrawTargetRecording::PushClip(const Path* aPath) {
  if (!aPath) {
    return;
  }

  // The canvas doesn't have a clipRect API so we always end up in the generic
  // path. The D2D backend doesn't have a good way of specializing rectangular
  // clips so we take advantage of the fact that aPath is usually backed by a
  // SkiaPath which implements AsRect() and specialize it here.
  auto rect = aPath->AsRect();
  if (rect.isSome()) {
    PushClipRect(rect.value());
    return;
  }

  RefPtr<PathRecording> pathRecording = EnsurePathStored(aPath);
  RecordEventSelf(RecordedPushClip(ReferencePtr(pathRecording)));
}

void DrawTargetRecording::PushClipRect(const Rect& aRect) {
  RecordEventSelf(RecordedPushClipRect(aRect));
}

void DrawTargetRecording::PopClip() {
  RecordEventSelfSkipFlushTransform(RecordedPopClip());
}

bool DrawTargetRecording::RemoveAllClips() {
  RecordEventSelfSkipFlushTransform(RecordedRemoveAllClips());
  return true;
}

void DrawTargetRecording::PushLayer(bool aOpaque, Float aOpacity,
                                    SourceSurface* aMask,
                                    const Matrix& aMaskTransform,
                                    const IntRect& aBounds,
                                    bool aCopyBackground) {
  if (aMask) {
    EnsureSurfaceStoredRecording(mRecorder, aMask, "PushLayer");
  }

  RecordEventSelf(RecordedPushLayer(aOpaque, aOpacity, aMask, aMaskTransform,
                                    aBounds, aCopyBackground));

  PushedLayer layer(GetPermitSubpixelAA());
  mPushedLayers.push_back(layer);
  DrawTarget::SetPermitSubpixelAA(aOpaque);
}

void DrawTargetRecording::PushLayerWithBlend(bool aOpaque, Float aOpacity,
                                             SourceSurface* aMask,
                                             const Matrix& aMaskTransform,
                                             const IntRect& aBounds,
                                             bool aCopyBackground,
                                             CompositionOp aCompositionOp) {
  if (aMask) {
    EnsureSurfaceStoredRecording(mRecorder, aMask, "PushLayer");
  }

  RecordEventSelf(RecordedPushLayerWithBlend(aOpaque, aOpacity, aMask,
                                             aMaskTransform, aBounds,
                                             aCopyBackground, aCompositionOp));

  PushedLayer layer(GetPermitSubpixelAA());
  mPushedLayers.push_back(layer);
  DrawTarget::SetPermitSubpixelAA(aOpaque);
}

void DrawTargetRecording::PopLayer() {
  MarkChanged();

  RecordEventSelfSkipFlushTransform(RecordedPopLayer());

  const PushedLayer& layer = mPushedLayers.back();
  DrawTarget::SetPermitSubpixelAA(layer.mOldPermitSubpixelAA);
  mPushedLayers.pop_back();
}

already_AddRefed<SourceSurface>
DrawTargetRecording::CreateSourceSurfaceFromData(unsigned char* aData,
                                                 const IntSize& aSize,
                                                 int32_t aStride,
                                                 SurfaceFormat aFormat) const {
  RefPtr<SourceSurface> surface = CreateDataSourceSurfaceWithStrideFromData(
      aSize, aFormat, aStride, aData, aStride);
  if (!surface) {
    return nullptr;
  }

  return OptimizeSourceSurface(surface);
}

already_AddRefed<SourceSurface> DrawTargetRecording::OptimizeSourceSurface(
    SourceSurface* aSurface) const {
  // See if we have a previously optimized surface available. We have to do this
  // check before the SurfaceType::RECORDING below, because aSurface might be a
  // SurfaceType::RECORDING from another recorder we have previously optimized.
  auto* userData = static_cast<RecordingSourceSurfaceUserData*>(
      aSurface->GetUserData(reinterpret_cast<UserDataKey*>(mRecorder.get())));
  if (userData) {
    RefPtr<SourceSurface> strongRef(userData->optimizedSurface);
    if (strongRef) {
      return do_AddRef(strongRef);
    }
  } else {
    if (!EnsureSurfaceStoredRecording(mRecorder, aSurface,
                                      "OptimizeSourceSurface")) {
      // Surface was already stored, but doesn't have UserData so must be one
      // of our recording surfaces.
      MOZ_ASSERT(aSurface->GetType() == SurfaceType::RECORDING);
      return do_AddRef(aSurface);
    }

    userData = static_cast<RecordingSourceSurfaceUserData*>(
        aSurface->GetUserData(reinterpret_cast<UserDataKey*>(mRecorder.get())));
    MOZ_ASSERT(userData,
               "User data should always have been set by "
               "EnsureSurfaceStoredRecording.");
  }

  RefPtr<SourceSurface> retSurf = new SourceSurfaceRecording(
      aSurface->GetSize(), aSurface->GetFormat(), mRecorder, aSurface);
  RecordEventSelfSkipFlushTransform(
      RecordedOptimizeSourceSurface(aSurface, retSurf));
  userData->optimizedSurface = retSurf;

  return retSurf.forget();
}

already_AddRefed<SourceSurface>
DrawTargetRecording::CreateSourceSurfaceFromNativeSurface(
    const NativeSurface& aSurface) const {
  MOZ_ASSERT(false);
  return nullptr;
}

already_AddRefed<DrawTarget>
DrawTargetRecording::CreateSimilarDrawTargetWithBacking(
    const IntSize& aSize, SurfaceFormat aFormat) const {
  if (mFinalDT->CanCreateSimilarDrawTarget(aSize, aFormat)) {
    // If the requested similar draw target is too big, then we should try to
    // rasterize on the content side to avoid duplicating the effort when a
    // blob image gets tiled. If we fail somehow to produce it, we can fall
    // back to recording.
    constexpr int32_t kRasterThreshold = 256 * 256 * 4;
    int32_t stride = aSize.width * BytesPerPixel(aFormat);
    int32_t surfaceBytes = aSize.height * stride;
    if (surfaceBytes >= kRasterThreshold) {
      auto surface = MakeRefPtr<SourceSurfaceSharedData>();
      if (surface->Init(aSize, stride, aFormat)) {
        auto dt = MakeRefPtr<DrawTargetSkia>();
        if (dt->Init(std::move(surface))) {
          return dt.forget();
        } else {
          MOZ_ASSERT_UNREACHABLE("Skia should initialize given surface!");
        }
      }
    }
  }

  return CreateSimilarDrawTarget(aSize, aFormat);
}

already_AddRefed<DrawTarget> DrawTargetRecording::CreateSimilarDrawTarget(
    const IntSize& aSize, SurfaceFormat aFormat) const {
  RefPtr<DrawTargetRecording> similarDT;
  if (mFinalDT->CanCreateSimilarDrawTarget(aSize, aFormat)) {
    similarDT =
        new DrawTargetRecording(this, IntRect(IntPoint(0, 0), aSize), aFormat);
    similarDT->SetOptimizeTransform(mOptimizeTransform);
    RecordEventSelfSkipFlushTransform(
        RecordedCreateSimilarDrawTarget(similarDT.get(), aSize, aFormat));
  } else if (XRE_IsContentProcess()) {
    // Crash any content process that calls this function with arguments that
    // would fail to create a similar draw target. We do this to root out bad
    // callers. We don't want to crash any important processes though so for
    // for those we'll just gracefully return nullptr.
    MOZ_CRASH(
        "Content-process DrawTargetRecording can't create requested similar "
        "drawtarget");
  }
  return similarDT.forget();
}

bool DrawTargetRecording::CanCreateSimilarDrawTarget(
    const IntSize& aSize, SurfaceFormat aFormat) const {
  return mFinalDT->CanCreateSimilarDrawTarget(aSize, aFormat);
}

RefPtr<DrawTarget> DrawTargetRecording::CreateClippedDrawTarget(
    const Rect& aBounds, SurfaceFormat aFormat) {
  RefPtr<DrawTargetRecording> similarDT =
      new DrawTargetRecording(this, mRect, aFormat);
  similarDT->SetOptimizeTransform(mOptimizeTransform);
  RecordEventSelf(
      RecordedCreateClippedDrawTarget(similarDT.get(), aBounds, aFormat));
  similarDT->mTransform = similarDT->mRecordedTransform = mTransform;
  return similarDT;
}

already_AddRefed<DrawTarget>
DrawTargetRecording::CreateSimilarDrawTargetForFilter(
    const IntSize& aMaxSize, SurfaceFormat aFormat, FilterNode* aFilter,
    FilterNode* aSource, const Rect& aSourceRect, const Point& aDestPoint) {
  RefPtr<DrawTargetRecording> similarDT;
  if (mFinalDT->CanCreateSimilarDrawTarget(aMaxSize, aFormat)) {
    similarDT = new DrawTargetRecording(this, IntRect(IntPoint(0, 0), aMaxSize),
                                        aFormat);
    similarDT->SetOptimizeTransform(mOptimizeTransform);
    // RecordedCreateDrawTargetForFilter::PlayEvent uses the transform, despite
    // the fact that the underlying DrawTarget does not.
    RecordEventSelf(RecordedCreateDrawTargetForFilter(similarDT.get(), aMaxSize,
                                                      aFormat, aFilter, aSource,
                                                      aSourceRect, aDestPoint));
  } else if (XRE_IsContentProcess()) {
    // See CreateSimilarDrawTarget
    MOZ_CRASH(
        "Content-process DrawTargetRecording can't create requested clipped "
        "drawtarget");
  }
  return similarDT.forget();
}

already_AddRefed<PathBuilder> DrawTargetRecording::CreatePathBuilder(
    FillRule aFillRule) const {
  return MakeAndAddRef<PathBuilderRecording>(mFinalDT->GetBackendType(),
                                             aFillRule);
}

already_AddRefed<GradientStops> DrawTargetRecording::CreateGradientStops(
    GradientStop* aStops, uint32_t aNumStops, ExtendMode aExtendMode) const {
  RefPtr<GradientStops> retStops = new GradientStopsRecording(mRecorder);

  RecordEventSelfSkipFlushTransform(
      RecordedGradientStopsCreation(retStops, aStops, aNumStops, aExtendMode));

  return retStops.forget();
}

void DrawTargetRecording::SetTransform(const Matrix& aTransform) {
  DrawTarget::SetTransform(aTransform);
  if (!mOptimizeTransform) {
    FlushTransform();
  }
}

void DrawTargetRecording::RecordTransform(const Matrix& aTransform) const {
  RecordEventSelfSkipFlushTransform(RecordedSetTransform(aTransform));
  mRecordedTransform = aTransform;
}

void DrawTargetRecording::SetPermitSubpixelAA(bool aPermitSubpixelAA) {
  if (aPermitSubpixelAA == mPermitSubpixelAA) {
    return;
  }
  DrawTarget::SetPermitSubpixelAA(aPermitSubpixelAA);
  RecordEventSelfSkipFlushTransform(
      RecordedSetPermitSubpixelAA(aPermitSubpixelAA));
}

already_AddRefed<PathRecording> DrawTargetRecording::EnsurePathStored(
    const Path* aPath) {
  RefPtr<PathRecording> pathRecording;
  if (aPath->GetBackendType() == BackendType::RECORDING) {
    pathRecording =
        const_cast<PathRecording*>(static_cast<const PathRecording*>(aPath));
    if (!mRecorder->TryAddStoredObject(pathRecording)) {
      // Path is already stored.
      return pathRecording.forget();
    }
  } else {
    MOZ_ASSERT(!mRecorder->HasStoredObject(aPath));
    FillRule fillRule = aPath->GetFillRule();
    RefPtr<PathBuilderRecording> builderRecording =
        new PathBuilderRecording(mFinalDT->GetBackendType(), fillRule);
    aPath->StreamToSink(builderRecording);
    pathRecording = builderRecording->Finish().downcast<PathRecording>();
    mRecorder->AddStoredObject(pathRecording);
  }

  // It's important that AddStoredObject or TryAddStoredObject is called before
  // this because that will run any pending processing required by recorded
  // objects that have been deleted off the main thread.
  RecordEventSelfSkipFlushTransform(RecordedPathCreation(pathRecording.get()));
  pathRecording->mStoredRecorders.push_back(mRecorder);

  return pathRecording.forget();
}

// This should only be called on the 'root' DrawTargetRecording.
// Calling it on a child DrawTargetRecordings will cause confusion.
void DrawTargetRecording::FlushItem(const IntRect& aBounds) {
  mRecorder->FlushItem(aBounds);
  // Reinitialize the recorder (FlushItem will write a new recording header)
  // Tell the new recording about our draw target
  // This code should match what happens in the DrawTargetRecording constructor.
  MOZ_DIAGNOSTIC_ASSERT(mRecorder->GetRecorderType() ==
                        RecorderType::WEBRENDER);
  RecordEventSkipFlushTransform(
      RecordedDrawTargetCreation(this, mFinalDT->GetBackendType(), mRect,
                                 mFinalDT->GetFormat(), false, nullptr));
  // RecordedDrawTargetCreation can actually reuse the base DrawTarget for the
  // recording, but we cannot conclude that from here, so force the transform
  // to be recorded.
  RecordTransform(mTransform);
  mTransformDirty = false;
}

void DrawTargetRecording::EnsurePatternDependenciesStored(
    const Pattern& aPattern) {
  switch (aPattern.GetType()) {
    case PatternType::COLOR:
      // No dependencies here.
      return;
    case PatternType::LINEAR_GRADIENT: {
      MOZ_ASSERT_IF(
          static_cast<const LinearGradientPattern*>(&aPattern)->mStops,
          mRecorder->HasStoredObject(
              static_cast<const LinearGradientPattern*>(&aPattern)->mStops));
      return;
    }
    case PatternType::RADIAL_GRADIENT: {
      MOZ_ASSERT_IF(
          static_cast<const RadialGradientPattern*>(&aPattern)->mStops,
          mRecorder->HasStoredObject(
              static_cast<const RadialGradientPattern*>(&aPattern)->mStops));
      return;
    }
    case PatternType::CONIC_GRADIENT: {
      MOZ_ASSERT_IF(
          static_cast<const ConicGradientPattern*>(&aPattern)->mStops,
          mRecorder->HasStoredObject(
              static_cast<const ConicGradientPattern*>(&aPattern)->mStops));
      return;
    }
    case PatternType::SURFACE: {
      const SurfacePattern* pat = static_cast<const SurfacePattern*>(&aPattern);
      EnsureSurfaceStoredRecording(mRecorder, pat->mSurface,
                                   "EnsurePatternDependenciesStored");
      return;
    }
  }
}

}  // namespace gfx
}  // namespace mozilla
