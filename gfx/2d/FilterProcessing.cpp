/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FilterProcessing.h"
#include "Logging.h"
#include "Swizzle.h"

#include "mozilla/SSE.h"

namespace mozilla {
namespace gfx {

already_AddRefed<DataSourceSurface> FilterProcessing::ExtractAlpha(
    DataSourceSurface* aSource) {
  IntSize size = aSource->GetSize();
  RefPtr<DataSourceSurface> alpha =
      Factory::CreateDataSourceSurface(size, SurfaceFormat::A8);
  if (MOZ2D_WARN_IF(!alpha)) {
    return nullptr;
  }

  DataSourceSurface::ScopedMap sourceMap(aSource, DataSourceSurface::READ);
  DataSourceSurface::ScopedMap alphaMap(alpha, DataSourceSurface::WRITE);
  if (MOZ2D_WARN_IF(!sourceMap.IsMapped() || !alphaMap.IsMapped())) {
    return nullptr;
  }

  uint8_t* sourceData = sourceMap.GetData();
  int32_t sourceStride = sourceMap.GetStride();
  uint8_t* alphaData = alphaMap.GetData();
  int32_t alphaStride = alphaMap.GetStride();

  if (mozilla::supports_sse2()) {
#ifdef USE_SSE2
    ExtractAlpha_SSE2(size, sourceData, sourceStride, alphaData, alphaStride);
#endif
  } else {
    ExtractAlpha_Scalar(size, sourceData, sourceStride, alphaData, alphaStride);
  }

  return alpha.forget();
}

already_AddRefed<DataSourceSurface> FilterProcessing::ConvertToB8G8R8A8(
    SourceSurface* aSurface) {
  if (mozilla::supports_sse2()) {
#ifdef USE_SSE2
    return ConvertToB8G8R8A8_SSE2(aSurface);
#endif
  }
  return ConvertToB8G8R8A8_Scalar(aSurface);
}

already_AddRefed<DataSourceSurface> FilterProcessing::ApplyBlending(
    DataSourceSurface* aInput1, DataSourceSurface* aInput2,
    BlendMode aBlendMode) {
  if (mozilla::supports_sse2()) {
#ifdef USE_SSE2
    return ApplyBlending_SSE2(aInput1, aInput2, aBlendMode);
#endif
  }
  return nullptr;
}

void FilterProcessing::ApplyMorphologyHorizontal(
    const uint8_t* aSourceData, int32_t aSourceStride, uint8_t* aDestData,
    int32_t aDestStride, const IntRect& aDestRect, int32_t aRadius,
    MorphologyOperator aOp) {
  if (mozilla::supports_sse2()) {
#ifdef USE_SSE2
    ApplyMorphologyHorizontal_SSE2(aSourceData, aSourceStride, aDestData,
                                   aDestStride, aDestRect, aRadius, aOp);
#endif
  } else {
    ApplyMorphologyHorizontal_Scalar(aSourceData, aSourceStride, aDestData,
                                     aDestStride, aDestRect, aRadius, aOp);
  }
}

void FilterProcessing::ApplyMorphologyVertical(
    const uint8_t* aSourceData, int32_t aSourceStride, uint8_t* aDestData,
    int32_t aDestStride, const IntRect& aDestRect, int32_t aRadius,
    MorphologyOperator aOp) {
  if (mozilla::supports_sse2()) {
#ifdef USE_SSE2
    ApplyMorphologyVertical_SSE2(aSourceData, aSourceStride, aDestData,
                                 aDestStride, aDestRect, aRadius, aOp);
#endif
  } else {
    ApplyMorphologyVertical_Scalar(aSourceData, aSourceStride, aDestData,
                                   aDestStride, aDestRect, aRadius, aOp);
  }
}

already_AddRefed<DataSourceSurface> FilterProcessing::ApplyColorMatrix(
    DataSourceSurface* aInput, const Matrix5x4& aMatrix) {
  if (mozilla::supports_sse2()) {
#ifdef USE_SSE2
    return ApplyColorMatrix_SSE2(aInput, aMatrix);
#endif
  }
  return ApplyColorMatrix_Scalar(aInput, aMatrix);
}

void FilterProcessing::ApplyComposition(DataSourceSurface* aSource,
                                        DataSourceSurface* aDest,
                                        CompositeOperator aOperator) {
  if (mozilla::supports_sse2()) {
#ifdef USE_SSE2
    ApplyComposition_SSE2(aSource, aDest, aOperator);
#endif
  } else {
    ApplyComposition_Scalar(aSource, aDest, aOperator);
  }
}

void FilterProcessing::DoPremultiplicationCalculation(
    const IntSize& aSize, uint8_t* aTargetData, int32_t aTargetStride,
    const uint8_t* aSourceData, int32_t aSourceStride) {
  PremultiplyData(aSourceData, aSourceStride, SurfaceFormat::B8G8R8A8,
                  aTargetData, aTargetStride, SurfaceFormat::B8G8R8A8, aSize);
}

void FilterProcessing::DoUnpremultiplicationCalculation(
    const IntSize& aSize, uint8_t* aTargetData, int32_t aTargetStride,
    const uint8_t* aSourceData, int32_t aSourceStride) {
  UnpremultiplyData(aSourceData, aSourceStride, SurfaceFormat::B8G8R8A8,
                    aTargetData, aTargetStride, SurfaceFormat::B8G8R8A8, aSize);
}

void FilterProcessing::DoOpacityCalculation(
    const IntSize& aSize, uint8_t* aTargetData, int32_t aTargetStride,
    const uint8_t* aSourceData, int32_t aSourceStride, Float aValue) {
  if (mozilla::supports_sse2()) {
#ifdef USE_SSE2
    DoOpacityCalculation_SSE2(aSize, aTargetData, aTargetStride, aSourceData,
                              aSourceStride, aValue);
#endif
  } else {
    DoOpacityCalculation_Scalar(aSize, aTargetData, aTargetStride, aSourceData,
                                aSourceStride, aValue);
  }
}

void FilterProcessing::DoOpacityCalculationA8(
    const IntSize& aSize, uint8_t* aTargetData, int32_t aTargetStride,
    const uint8_t* aSourceData, int32_t aSourceStride, Float aValue) {
  DoOpacityCalculationA8_Scalar(aSize, aTargetData, aTargetStride, aSourceData,
                                aSourceStride, aValue);
}

already_AddRefed<DataSourceSurface> FilterProcessing::RenderTurbulence(
    const IntSize& aSize, const Point& aOffset, const Size& aBaseFrequency,
    int32_t aSeed, int aNumOctaves, TurbulenceType aType, bool aStitch,
    const Rect& aTileRect) {
  if (mozilla::supports_sse2()) {
#ifdef USE_SSE2
    return RenderTurbulence_SSE2(aSize, aOffset, aBaseFrequency, aSeed,
                                 aNumOctaves, aType, aStitch, aTileRect);
#endif
  }
  return RenderTurbulence_Scalar(aSize, aOffset, aBaseFrequency, aSeed,
                                 aNumOctaves, aType, aStitch, aTileRect);
}

already_AddRefed<DataSourceSurface> FilterProcessing::ApplyArithmeticCombine(
    DataSourceSurface* aInput1, DataSourceSurface* aInput2, Float aK1,
    Float aK2, Float aK3, Float aK4) {
  if (mozilla::supports_sse2()) {
#ifdef USE_SSE2
    return ApplyArithmeticCombine_SSE2(aInput1, aInput2, aK1, aK2, aK3, aK4);
#endif
  }
  return ApplyArithmeticCombine_Scalar(aInput1, aInput2, aK1, aK2, aK3, aK4);
}

}  // namespace gfx
}  // namespace mozilla
