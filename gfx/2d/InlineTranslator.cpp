/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "InlineTranslator.h"

#include "RecordedEventImpl.h"
#include "mozilla/gfx/RecordingTypes.h"

using namespace mozilla::gfx;

namespace mozilla::gfx {

InlineTranslator::InlineTranslator() : mFontContext(nullptr) {}

InlineTranslator::InlineTranslator(DrawTarget* aDT, void* aFontContext)
    : mBaseDT(aDT), mFontContext(aFontContext) {}

bool InlineTranslator::TranslateRecording(char* aData, size_t aLen) {
  MemReader reader(aData, aLen);

  uint32_t magicInt;
  ReadElement(reader, magicInt);
  if (magicInt != mozilla::gfx::kMagicInt) {
    mError = "Magic";
    return false;
  }

  uint16_t majorRevision;
  ReadElement(reader, majorRevision);
  if (majorRevision != kMajorRevision) {
    mError = "Major";
    return false;
  }

  uint16_t minorRevision;
  ReadElement(reader, minorRevision);
  if (minorRevision > kMinorRevision) {
    mError = "Minor";
    return false;
  }

  uint8_t eventType;
  ReadElement(reader, eventType);
  while (reader.good()) {
    bool success = RecordedEvent::DoWithEvent(
        reader, static_cast<RecordedEvent::EventType>(eventType),
        [&](RecordedEvent* recordedEvent) -> bool {
          // Make sure that the whole event was read from the stream
          // successfully.
          if (!reader.good()) {
            mError = " READ";
            return false;
          }

          if (!recordedEvent->PlayEvent(this)) {
            mError = " PLAY";
            return false;
          }

          return true;
        });
    if (!success) {
      mError = RecordedEvent::GetEventName(
                   static_cast<RecordedEvent::EventType>(eventType)) +
               mError;
      return false;
    }

    ReadElement(reader, eventType);
  }

  return true;
}

already_AddRefed<DrawTarget> InlineTranslator::CreateDrawTarget(
    ReferencePtr aRefPtr, const gfx::IntSize& aSize,
    gfx::SurfaceFormat aFormat) {
  MOZ_ASSERT(mBaseDT, "mBaseDT has not been initialized.");

  RefPtr<DrawTarget> drawTarget = mBaseDT;
  AddDrawTarget(aRefPtr, drawTarget);
  return drawTarget.forget();
}

template <typename Table>
static void CopyTable(Table& aDest, const Table& aSrc) {
  aDest = Table(aSrc.Count());
  for (const auto& entry : aSrc) {
    aDest.InsertOrUpdate(entry.GetKey(), entry.GetData());
  }
}

void InlineTranslator::SaveState(State& aState) const {
  aState.mCurrentDT = mCurrentDT;
  CopyTable(aState.mDrawTargets, mDrawTargets);
  CopyTable(aState.mPaths, mPaths);
  CopyTable(aState.mSourceSurfaces, mSourceSurfaces);
  CopyTable(aState.mFilterNodes, mFilterNodes);
  CopyTable(aState.mGradientStops, mGradientStops);
  CopyTable(aState.mScaledFonts, mScaledFonts);
  CopyTable(aState.mUnscaledFonts, mUnscaledFonts);
  CopyTable(aState.mNativeFontResources, mNativeFontResources);
}

void InlineTranslator::RestoreState(const State& aState) {
  mCurrentDT = aState.mCurrentDT;
  CopyTable(mDrawTargets, aState.mDrawTargets);
  CopyTable(mPaths, aState.mPaths);
  CopyTable(mSourceSurfaces, aState.mSourceSurfaces);
  CopyTable(mFilterNodes, aState.mFilterNodes);
  CopyTable(mGradientStops, aState.mGradientStops);
  CopyTable(mScaledFonts, aState.mScaledFonts);
  CopyTable(mUnscaledFonts, aState.mUnscaledFonts);
  CopyTable(mNativeFontResources, aState.mNativeFontResources);
}

already_AddRefed<SourceSurface> InlineTranslator::LookupExternalSurface(
    uint64_t aKey) {
  if (!mExternalSurfaces) {
    return nullptr;
  }
  RefPtr<SourceSurface> surface = mExternalSurfaces->Get(aKey);
  return surface.forget();
}

}  // namespace mozilla::gfx
