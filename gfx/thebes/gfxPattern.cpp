/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gfxPattern.h"

#include "gfxUtils.h"
#include "gfxTypes.h"
#include "gfxPlatform.h"
#include "gfx2DGlue.h"
#include "gfxGradientCache.h"
#include "mozilla/gfx/2D.h"

#include "cairo.h"

#include <vector>

using namespace mozilla::gfx;

gfxPattern::gfxPattern(const DeviceColor& aColor) : mExtend(ExtendMode::CLAMP) {
  mGfxPattern.InitColorPattern(aColor);
}

// linear
gfxPattern::gfxPattern(gfxFloat x0, gfxFloat y0, gfxFloat x1, gfxFloat y1)
    : mExtend(ExtendMode::CLAMP) {
  mGfxPattern.InitLinearGradientPattern(Point(x0, y0), Point(x1, y1), nullptr);
}

// radial
gfxPattern::gfxPattern(gfxFloat cx0, gfxFloat cy0, gfxFloat radius0,
                       gfxFloat cx1, gfxFloat cy1, gfxFloat radius1)
    : mExtend(ExtendMode::CLAMP) {
  mGfxPattern.InitRadialGradientPattern(Point(cx0, cy0), Point(cx1, cy1),
                                        radius0, radius1, nullptr);
}

// conic
gfxPattern::gfxPattern(gfxFloat cx, gfxFloat cy, gfxFloat angle,
                       gfxFloat startOffset, gfxFloat endOffset)
    : mExtend(ExtendMode::CLAMP) {
  mGfxPattern.InitConicGradientPattern(Point(cx, cy), angle, startOffset,
                                       endOffset, nullptr);
}

// Azure
gfxPattern::gfxPattern(SourceSurface* aSurface,
                       const Matrix& aPatternToUserSpace)
    : mPatternToUserSpace(aPatternToUserSpace), mExtend(ExtendMode::CLAMP) {
  mGfxPattern.InitSurfacePattern(
      aSurface, mExtend, Matrix(),  // matrix is overridden in GetPattern()
      mozilla::gfx::SamplingFilter::GOOD);
}

void gfxPattern::AddColorStop(gfxFloat offset, const DeviceColor& c) {
  if (mGfxPattern.GetPattern()->GetType() != PatternType::LINEAR_GRADIENT &&
      mGfxPattern.GetPattern()->GetType() != PatternType::RADIAL_GRADIENT &&
      mGfxPattern.GetPattern()->GetType() != PatternType::CONIC_GRADIENT) {
    return;
  }

  mStops = nullptr;

  GradientStop stop;
  stop.offset = offset;
  stop.color = c;
  mStopsList.AppendElement(stop);
}

void gfxPattern::SetColorStops(GradientStops* aStops) { mStops = aStops; }

void gfxPattern::CacheColorStops(const DrawTarget* aDT) {
  mStops = gfxGradientCache::GetOrCreateGradientStops(aDT, mStopsList, mExtend);
}

void gfxPattern::SetMatrix(const gfxMatrix& aPatternToUserSpace) {
  mPatternToUserSpace = ToMatrix(aPatternToUserSpace);
  // Cairo-pattern matrices specify the conversion from DrawTarget to pattern
  // space. Azure pattern matrices specify the conversion from pattern to
  // DrawTarget space.
  mPatternToUserSpace.Invert();
}

gfxMatrix gfxPattern::GetMatrix() const {
  // invert at the higher precision of gfxMatrix
  // cause we need to convert at some point anyways
  gfxMatrix mat = ThebesMatrix(mPatternToUserSpace);
  mat.Invert();
  return mat;
}

gfxMatrix gfxPattern::GetInverseMatrix() const {
  return ThebesMatrix(mPatternToUserSpace);
}

Pattern* gfxPattern::GetPattern(const DrawTarget* aTarget,
                                const Matrix* aOriginalUserToDevice) {
  Matrix patternToUser = mPatternToUserSpace;

  if (aOriginalUserToDevice &&
      !aOriginalUserToDevice->FuzzyEquals(aTarget->GetTransform())) {
    // mPatternToUserSpace maps from pattern space to the original user space,
    // but aTarget now has a transform to a different user space.  In order for
    // the Pattern* that we return to be usable in aTarget's new user space we
    // need the Pattern's mMatrix to be the transform from pattern space to
    // aTarget's -new- user space.  That transform is equivalent to the
    // transform from pattern space to original user space (patternToUser),
    // multiplied by the transform from original user space to device space,
    // multiplied by the transform from device space to current user space.

    Matrix deviceToCurrentUser = aTarget->GetTransform();
    deviceToCurrentUser.Invert();

    patternToUser =
        patternToUser * *aOriginalUserToDevice * deviceToCurrentUser;
  }
  patternToUser.NudgeToIntegers();

  // Ensure stops are compatible with the DT.
  if (!mStopsList.IsEmpty() &&
      (!mStops || mStops->GetBackendType() != aTarget->GetBackendType())) {
    mStops = aTarget->CreateGradientStops(mStopsList.Elements(),
                                          mStopsList.Length(), mExtend);
  }

  switch (mGfxPattern.GetPattern()->GetType()) {
    case PatternType::SURFACE: {
      SurfacePattern* surfacePattern =
          static_cast<SurfacePattern*>(mGfxPattern.GetPattern());
      surfacePattern->mMatrix = patternToUser;
      surfacePattern->mExtendMode = mExtend;
      break;
    }
    case PatternType::LINEAR_GRADIENT: {
      LinearGradientPattern* linearGradientPattern =
          static_cast<LinearGradientPattern*>(mGfxPattern.GetPattern());
      linearGradientPattern->mMatrix = patternToUser;
      linearGradientPattern->mStops = mStops;
      break;
    }
    case PatternType::RADIAL_GRADIENT: {
      RadialGradientPattern* radialGradientPattern =
          static_cast<RadialGradientPattern*>(mGfxPattern.GetPattern());
      radialGradientPattern->mMatrix = patternToUser;
      radialGradientPattern->mStops = mStops;
      break;
    }
    case PatternType::CONIC_GRADIENT: {
      ConicGradientPattern* conicGradientPattern =
          static_cast<ConicGradientPattern*>(mGfxPattern.GetPattern());
      conicGradientPattern->mMatrix = patternToUser;
      conicGradientPattern->mStops = mStops;
      break;
    }
    default:
      /* Reassure the compiler we are handling all the enum values.  */
      break;
  }

  return mGfxPattern.GetPattern();
}

void gfxPattern::SetExtend(ExtendMode aExtend) {
  mExtend = aExtend;
  mStops = nullptr;
}

bool gfxPattern::IsOpaque() {
  if (mGfxPattern.GetPattern()->GetType() != PatternType::SURFACE) {
    return false;
  }

  if (static_cast<SurfacePattern*>(mGfxPattern.GetPattern())
          ->mSurface->GetFormat() == SurfaceFormat::B8G8R8X8) {
    return true;
  }
  return false;
}

void gfxPattern::SetSamplingFilter(mozilla::gfx::SamplingFilter filter) {
  if (mGfxPattern.GetPattern()->GetType() != PatternType::SURFACE) {
    return;
  }

  static_cast<SurfacePattern*>(mGfxPattern.GetPattern())->mSamplingFilter =
      filter;
}

SamplingFilter gfxPattern::SamplingFilter() const {
  if (mGfxPattern.GetPattern()->GetType() != PatternType::SURFACE) {
    return mozilla::gfx::SamplingFilter::GOOD;
  }
  return static_cast<const SurfacePattern*>(mGfxPattern.GetPattern())
      ->mSamplingFilter;
}

bool gfxPattern::GetSolidColor(DeviceColor& aColorOut) {
  if (mGfxPattern.GetPattern()->GetType() == PatternType::COLOR) {
    aColorOut = static_cast<ColorPattern*>(mGfxPattern.GetPattern())->mColor;
    return true;
  }

  return false;
}
