/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FilterSupport.h"
#include "FilterDescription.h"

#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Filters.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/PodOperations.h"

#include "gfxContext.h"
#include "gfxPattern.h"
#include "gfxPlatform.h"
#include "gfxUtils.h"
#include "gfx2DGlue.h"

#include "nsMargin.h"

// c = n / 255
// c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1 / 2.4f) - 0.055f
static const float glinearRGBTosRGBMap[256] = {
    0.000f, 0.050f, 0.085f, 0.111f, 0.132f, 0.150f, 0.166f, 0.181f, 0.194f,
    0.207f, 0.219f, 0.230f, 0.240f, 0.250f, 0.260f, 0.269f, 0.278f, 0.286f,
    0.295f, 0.303f, 0.310f, 0.318f, 0.325f, 0.332f, 0.339f, 0.346f, 0.352f,
    0.359f, 0.365f, 0.371f, 0.378f, 0.383f, 0.389f, 0.395f, 0.401f, 0.406f,
    0.412f, 0.417f, 0.422f, 0.427f, 0.433f, 0.438f, 0.443f, 0.448f, 0.452f,
    0.457f, 0.462f, 0.466f, 0.471f, 0.476f, 0.480f, 0.485f, 0.489f, 0.493f,
    0.498f, 0.502f, 0.506f, 0.510f, 0.514f, 0.518f, 0.522f, 0.526f, 0.530f,
    0.534f, 0.538f, 0.542f, 0.546f, 0.549f, 0.553f, 0.557f, 0.561f, 0.564f,
    0.568f, 0.571f, 0.575f, 0.579f, 0.582f, 0.586f, 0.589f, 0.592f, 0.596f,
    0.599f, 0.603f, 0.606f, 0.609f, 0.613f, 0.616f, 0.619f, 0.622f, 0.625f,
    0.629f, 0.632f, 0.635f, 0.638f, 0.641f, 0.644f, 0.647f, 0.650f, 0.653f,
    0.656f, 0.659f, 0.662f, 0.665f, 0.668f, 0.671f, 0.674f, 0.677f, 0.680f,
    0.683f, 0.685f, 0.688f, 0.691f, 0.694f, 0.697f, 0.699f, 0.702f, 0.705f,
    0.708f, 0.710f, 0.713f, 0.716f, 0.718f, 0.721f, 0.724f, 0.726f, 0.729f,
    0.731f, 0.734f, 0.737f, 0.739f, 0.742f, 0.744f, 0.747f, 0.749f, 0.752f,
    0.754f, 0.757f, 0.759f, 0.762f, 0.764f, 0.767f, 0.769f, 0.772f, 0.774f,
    0.776f, 0.779f, 0.781f, 0.784f, 0.786f, 0.788f, 0.791f, 0.793f, 0.795f,
    0.798f, 0.800f, 0.802f, 0.805f, 0.807f, 0.809f, 0.812f, 0.814f, 0.816f,
    0.818f, 0.821f, 0.823f, 0.825f, 0.827f, 0.829f, 0.832f, 0.834f, 0.836f,
    0.838f, 0.840f, 0.843f, 0.845f, 0.847f, 0.849f, 0.851f, 0.853f, 0.855f,
    0.857f, 0.860f, 0.862f, 0.864f, 0.866f, 0.868f, 0.870f, 0.872f, 0.874f,
    0.876f, 0.878f, 0.880f, 0.882f, 0.884f, 0.886f, 0.888f, 0.890f, 0.892f,
    0.894f, 0.896f, 0.898f, 0.900f, 0.902f, 0.904f, 0.906f, 0.908f, 0.910f,
    0.912f, 0.914f, 0.916f, 0.918f, 0.920f, 0.922f, 0.924f, 0.926f, 0.928f,
    0.930f, 0.931f, 0.933f, 0.935f, 0.937f, 0.939f, 0.941f, 0.943f, 0.945f,
    0.946f, 0.948f, 0.950f, 0.952f, 0.954f, 0.956f, 0.957f, 0.959f, 0.961f,
    0.963f, 0.965f, 0.967f, 0.968f, 0.970f, 0.972f, 0.974f, 0.975f, 0.977f,
    0.979f, 0.981f, 0.983f, 0.984f, 0.986f, 0.988f, 0.990f, 0.991f, 0.993f,
    0.995f, 0.997f, 0.998f, 1.000f};

// c = n / 255
// c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f)
static const float gsRGBToLinearRGBMap[256] = {
    0.000f, 0.000f, 0.001f, 0.001f, 0.001f, 0.002f, 0.002f, 0.002f, 0.002f,
    0.003f, 0.003f, 0.003f, 0.004f, 0.004f, 0.004f, 0.005f, 0.005f, 0.006f,
    0.006f, 0.007f, 0.007f, 0.007f, 0.008f, 0.009f, 0.009f, 0.010f, 0.010f,
    0.011f, 0.012f, 0.012f, 0.013f, 0.014f, 0.014f, 0.015f, 0.016f, 0.017f,
    0.018f, 0.019f, 0.019f, 0.020f, 0.021f, 0.022f, 0.023f, 0.024f, 0.025f,
    0.026f, 0.027f, 0.028f, 0.030f, 0.031f, 0.032f, 0.033f, 0.034f, 0.036f,
    0.037f, 0.038f, 0.040f, 0.041f, 0.042f, 0.044f, 0.045f, 0.047f, 0.048f,
    0.050f, 0.051f, 0.053f, 0.054f, 0.056f, 0.058f, 0.060f, 0.061f, 0.063f,
    0.065f, 0.067f, 0.068f, 0.070f, 0.072f, 0.074f, 0.076f, 0.078f, 0.080f,
    0.082f, 0.084f, 0.087f, 0.089f, 0.091f, 0.093f, 0.095f, 0.098f, 0.100f,
    0.102f, 0.105f, 0.107f, 0.109f, 0.112f, 0.114f, 0.117f, 0.120f, 0.122f,
    0.125f, 0.127f, 0.130f, 0.133f, 0.136f, 0.138f, 0.141f, 0.144f, 0.147f,
    0.150f, 0.153f, 0.156f, 0.159f, 0.162f, 0.165f, 0.168f, 0.171f, 0.175f,
    0.178f, 0.181f, 0.184f, 0.188f, 0.191f, 0.195f, 0.198f, 0.202f, 0.205f,
    0.209f, 0.212f, 0.216f, 0.220f, 0.223f, 0.227f, 0.231f, 0.235f, 0.238f,
    0.242f, 0.246f, 0.250f, 0.254f, 0.258f, 0.262f, 0.266f, 0.270f, 0.275f,
    0.279f, 0.283f, 0.287f, 0.292f, 0.296f, 0.301f, 0.305f, 0.309f, 0.314f,
    0.319f, 0.323f, 0.328f, 0.332f, 0.337f, 0.342f, 0.347f, 0.352f, 0.356f,
    0.361f, 0.366f, 0.371f, 0.376f, 0.381f, 0.386f, 0.392f, 0.397f, 0.402f,
    0.407f, 0.413f, 0.418f, 0.423f, 0.429f, 0.434f, 0.440f, 0.445f, 0.451f,
    0.456f, 0.462f, 0.468f, 0.474f, 0.479f, 0.485f, 0.491f, 0.497f, 0.503f,
    0.509f, 0.515f, 0.521f, 0.527f, 0.533f, 0.539f, 0.546f, 0.552f, 0.558f,
    0.565f, 0.571f, 0.578f, 0.584f, 0.591f, 0.597f, 0.604f, 0.610f, 0.617f,
    0.624f, 0.631f, 0.638f, 0.644f, 0.651f, 0.658f, 0.665f, 0.672f, 0.680f,
    0.687f, 0.694f, 0.701f, 0.708f, 0.716f, 0.723f, 0.730f, 0.738f, 0.745f,
    0.753f, 0.761f, 0.768f, 0.776f, 0.784f, 0.791f, 0.799f, 0.807f, 0.815f,
    0.823f, 0.831f, 0.839f, 0.847f, 0.855f, 0.863f, 0.871f, 0.880f, 0.888f,
    0.896f, 0.905f, 0.913f, 0.922f, 0.930f, 0.939f, 0.947f, 0.956f, 0.965f,
    0.973f, 0.982f, 0.991f, 1.000f};

namespace mozilla {
namespace gfx {

// Some convenience FilterNode creation functions.

namespace FilterWrappers {

static already_AddRefed<FilterNode> Unpremultiply(DrawTarget* aDT,
                                                  FilterNode* aInput) {
  RefPtr<FilterNode> filter = aDT->CreateFilter(FilterType::UNPREMULTIPLY);
  if (filter) {
    filter->SetInput(IN_UNPREMULTIPLY_IN, aInput);
    return filter.forget();
  }
  return nullptr;
}

static already_AddRefed<FilterNode> Premultiply(DrawTarget* aDT,
                                                FilterNode* aInput) {
  RefPtr<FilterNode> filter = aDT->CreateFilter(FilterType::PREMULTIPLY);
  if (filter) {
    filter->SetInput(IN_PREMULTIPLY_IN, aInput);
    return filter.forget();
  }
  return nullptr;
}

static already_AddRefed<FilterNode> LinearRGBToSRGB(DrawTarget* aDT,
                                                    FilterNode* aInput) {
  RefPtr<FilterNode> transfer =
      aDT->CreateFilter(FilterType::DISCRETE_TRANSFER);
  if (transfer) {
    transfer->SetAttribute(ATT_DISCRETE_TRANSFER_DISABLE_R, false);
    transfer->SetAttribute(ATT_DISCRETE_TRANSFER_TABLE_R, glinearRGBTosRGBMap,
                           256);
    transfer->SetAttribute(ATT_DISCRETE_TRANSFER_DISABLE_G, false);
    transfer->SetAttribute(ATT_DISCRETE_TRANSFER_TABLE_G, glinearRGBTosRGBMap,
                           256);
    transfer->SetAttribute(ATT_DISCRETE_TRANSFER_DISABLE_B, false);
    transfer->SetAttribute(ATT_DISCRETE_TRANSFER_TABLE_B, glinearRGBTosRGBMap,
                           256);
    transfer->SetAttribute(ATT_DISCRETE_TRANSFER_DISABLE_A, true);
    transfer->SetInput(IN_DISCRETE_TRANSFER_IN, aInput);
    return transfer.forget();
  }
  return nullptr;
}

static already_AddRefed<FilterNode> SRGBToLinearRGB(DrawTarget* aDT,
                                                    FilterNode* aInput) {
  RefPtr<FilterNode> transfer =
      aDT->CreateFilter(FilterType::DISCRETE_TRANSFER);
  if (transfer) {
    transfer->SetAttribute(ATT_DISCRETE_TRANSFER_DISABLE_R, false);
    transfer->SetAttribute(ATT_DISCRETE_TRANSFER_TABLE_R, gsRGBToLinearRGBMap,
                           256);
    transfer->SetAttribute(ATT_DISCRETE_TRANSFER_DISABLE_G, false);
    transfer->SetAttribute(ATT_DISCRETE_TRANSFER_TABLE_G, gsRGBToLinearRGBMap,
                           256);
    transfer->SetAttribute(ATT_DISCRETE_TRANSFER_DISABLE_B, false);
    transfer->SetAttribute(ATT_DISCRETE_TRANSFER_TABLE_B, gsRGBToLinearRGBMap,
                           256);
    transfer->SetAttribute(ATT_DISCRETE_TRANSFER_DISABLE_A, true);
    transfer->SetInput(IN_DISCRETE_TRANSFER_IN, aInput);
    return transfer.forget();
  }
  return nullptr;
}

sRGBColor SRGBToLinearRGB(const sRGBColor& color) {
  return sRGBColor(gsRGBToLinearRGBMap[uint8_t(color.r * 255)],
                   gsRGBToLinearRGBMap[uint8_t(color.g * 255)],
                   gsRGBToLinearRGBMap[uint8_t(color.b * 255)], color.a);
}

static already_AddRefed<FilterNode> Crop(DrawTarget* aDT,
                                         FilterNode* aInputFilter,
                                         const IntRect& aRect) {
  RefPtr<FilterNode> filter = aDT->CreateFilter(FilterType::CROP);
  if (filter) {
    filter->SetAttribute(ATT_CROP_RECT, Rect(aRect));
    filter->SetInput(IN_CROP_IN, aInputFilter);
    return filter.forget();
  }
  return nullptr;
}

static already_AddRefed<FilterNode> Offset(DrawTarget* aDT,
                                           FilterNode* aInputFilter,
                                           const IntPoint& aOffset) {
  if (aOffset == IntPoint()) {
    RefPtr<FilterNode> filter(aInputFilter);
    return filter.forget();
  }
  RefPtr<FilterNode> filter = aDT->CreateFilter(FilterType::TRANSFORM);
  if (filter) {
    filter->SetAttribute(ATT_TRANSFORM_MATRIX,
                         Matrix::Translation(aOffset.x, aOffset.y));
    filter->SetInput(IN_TRANSFORM_IN, aInputFilter);
    return filter.forget();
  }
  return nullptr;
}

static already_AddRefed<FilterNode> GaussianBlur(DrawTarget* aDT,
                                                 FilterNode* aInputFilter,
                                                 const Size& aStdDeviation) {
  if (aStdDeviation == Size()) {
    RefPtr<FilterNode> filter(aInputFilter);
    return filter.forget();
  }
  float stdX = float(std::min(aStdDeviation.width, kMaxStdDeviation));
  float stdY = float(std::min(aStdDeviation.height, kMaxStdDeviation));
  if (stdX == stdY) {
    RefPtr<FilterNode> filter = aDT->CreateFilter(FilterType::GAUSSIAN_BLUR);
    if (filter) {
      filter->SetAttribute(ATT_GAUSSIAN_BLUR_STD_DEVIATION, stdX);
      filter->SetInput(IN_GAUSSIAN_BLUR_IN, aInputFilter);
      return filter.forget();
    }
    return nullptr;
  }
  RefPtr<FilterNode> filterH = aDT->CreateFilter(FilterType::DIRECTIONAL_BLUR);
  RefPtr<FilterNode> filterV = aDT->CreateFilter(FilterType::DIRECTIONAL_BLUR);
  if (filterH && filterV) {
    filterH->SetAttribute(ATT_DIRECTIONAL_BLUR_DIRECTION,
                          (uint32_t)BLUR_DIRECTION_X);
    filterH->SetAttribute(ATT_DIRECTIONAL_BLUR_STD_DEVIATION, stdX);
    filterV->SetAttribute(ATT_DIRECTIONAL_BLUR_DIRECTION,
                          (uint32_t)BLUR_DIRECTION_Y);
    filterV->SetAttribute(ATT_DIRECTIONAL_BLUR_STD_DEVIATION, stdY);
    filterH->SetInput(IN_DIRECTIONAL_BLUR_IN, aInputFilter);
    filterV->SetInput(IN_DIRECTIONAL_BLUR_IN, filterH);
    return filterV.forget();
  }
  return nullptr;
}

already_AddRefed<FilterNode> Clear(DrawTarget* aDT) {
  RefPtr<FilterNode> filter = aDT->CreateFilter(FilterType::FLOOD);
  if (filter) {
    filter->SetAttribute(ATT_FLOOD_COLOR, DeviceColor());
    return filter.forget();
  }
  return nullptr;
}

already_AddRefed<FilterNode> ForSurface(DrawTarget* aDT,
                                        SourceSurface* aSurface,
                                        const IntPoint& aSurfacePosition) {
  RefPtr<FilterNode> filter = aDT->CreateFilter(FilterType::TRANSFORM);
  if (filter) {
    filter->SetAttribute(
        ATT_TRANSFORM_MATRIX,
        Matrix::Translation(aSurfacePosition.x, aSurfacePosition.y));
    filter->SetInput(IN_TRANSFORM_IN, aSurface);
    return filter.forget();
  }
  return nullptr;
}

static already_AddRefed<FilterNode> ToAlpha(DrawTarget* aDT,
                                            FilterNode* aInput) {
  float zero = 0.0f;
  RefPtr<FilterNode> transfer =
      aDT->CreateFilter(FilterType::DISCRETE_TRANSFER);
  if (transfer) {
    transfer->SetAttribute(ATT_DISCRETE_TRANSFER_DISABLE_R, false);
    transfer->SetAttribute(ATT_DISCRETE_TRANSFER_TABLE_R, &zero, 1);
    transfer->SetAttribute(ATT_DISCRETE_TRANSFER_DISABLE_G, false);
    transfer->SetAttribute(ATT_DISCRETE_TRANSFER_TABLE_G, &zero, 1);
    transfer->SetAttribute(ATT_DISCRETE_TRANSFER_DISABLE_B, false);
    transfer->SetAttribute(ATT_DISCRETE_TRANSFER_TABLE_B, &zero, 1);
    transfer->SetAttribute(ATT_DISCRETE_TRANSFER_DISABLE_A, true);
    transfer->SetInput(IN_DISCRETE_TRANSFER_IN, aInput);
    return transfer.forget();
  }
  return nullptr;
}

}  // namespace FilterWrappers

// A class that wraps a FilterNode and handles conversion between different
// color models. Create FilterCachedColorModels with your original filter and
// the color model that this filter outputs in natively, and then call
// ->ForColorModel(colorModel) in order to get a FilterNode which outputs to
// the specified colorModel.
// Internally, this is achieved by wrapping the original FilterNode with
// conversion FilterNodes. These filter nodes are cached in such a way that no
// repeated or back-and-forth conversions happen.
class FilterCachedColorModels {
 public:
  NS_INLINE_DECL_REFCOUNTING(FilterCachedColorModels)
  // aFilter can be null. In that case, ForColorModel will return a non-null
  // completely transparent filter for all color models.
  FilterCachedColorModels(DrawTarget* aDT, FilterNode* aFilter,
                          ColorModel aOriginalColorModel);

  // Get a FilterNode for the specified color model, guaranteed to be non-null.
  already_AddRefed<FilterNode> ForColorModel(ColorModel aColorModel);

  AlphaModel OriginalAlphaModel() const {
    return mOriginalColorModel.mAlphaModel;
  }

 private:
  // Create the required FilterNode that will be cached by ForColorModel.
  already_AddRefed<FilterNode> WrapForColorModel(ColorModel aColorModel);

  RefPtr<DrawTarget> mDT;
  ColorModel mOriginalColorModel;

  // This array is indexed by ColorModel::ToIndex.
  RefPtr<FilterNode> mFilterForColorModel[4];

  ~FilterCachedColorModels() = default;
};

FilterCachedColorModels::FilterCachedColorModels(DrawTarget* aDT,
                                                 FilterNode* aFilter,
                                                 ColorModel aOriginalColorModel)
    : mDT(aDT), mOriginalColorModel(aOriginalColorModel) {
  if (aFilter) {
    mFilterForColorModel[aOriginalColorModel.ToIndex()] = aFilter;
  } else {
    RefPtr<FilterNode> clear = FilterWrappers::Clear(aDT);
    mFilterForColorModel[0] = clear;
    mFilterForColorModel[1] = clear;
    mFilterForColorModel[2] = clear;
    mFilterForColorModel[3] = clear;
  }
}

already_AddRefed<FilterNode> FilterCachedColorModels::ForColorModel(
    ColorModel aColorModel) {
  if (aColorModel == mOriginalColorModel) {
    // Make sure to not call WrapForColorModel if our original filter node was
    // null, because then we'd get an infinite recursion.
    RefPtr<FilterNode> filter =
        mFilterForColorModel[mOriginalColorModel.ToIndex()];
    return filter.forget();
  }

  if (!mFilterForColorModel[aColorModel.ToIndex()]) {
    mFilterForColorModel[aColorModel.ToIndex()] =
        WrapForColorModel(aColorModel);
  }
  RefPtr<FilterNode> filter(mFilterForColorModel[aColorModel.ToIndex()]);
  return filter.forget();
}

already_AddRefed<FilterNode> FilterCachedColorModels::WrapForColorModel(
    ColorModel aColorModel) {
  // Convert one aspect at a time and recurse.
  // Conversions between premultiplied / unpremultiplied color channels for the
  // same color space can happen directly.
  // Conversions between different color spaces can only happen on
  // unpremultiplied color channels.

  if (aColorModel.mAlphaModel == AlphaModel::Premultiplied) {
    RefPtr<FilterNode> unpre = ForColorModel(
        ColorModel(aColorModel.mColorSpace, AlphaModel::Unpremultiplied));
    return FilterWrappers::Premultiply(mDT, unpre);
  }

  MOZ_ASSERT(aColorModel.mAlphaModel == AlphaModel::Unpremultiplied);
  if (aColorModel.mColorSpace == mOriginalColorModel.mColorSpace) {
    RefPtr<FilterNode> premultiplied = ForColorModel(
        ColorModel(aColorModel.mColorSpace, AlphaModel::Premultiplied));
    return FilterWrappers::Unpremultiply(mDT, premultiplied);
  }

  RefPtr<FilterNode> unpremultipliedOriginal = ForColorModel(
      ColorModel(mOriginalColorModel.mColorSpace, AlphaModel::Unpremultiplied));
  if (aColorModel.mColorSpace == ColorSpace::LinearRGB) {
    return FilterWrappers::SRGBToLinearRGB(mDT, unpremultipliedOriginal);
  }
  return FilterWrappers::LinearRGBToSRGB(mDT, unpremultipliedOriginal);
}

static const float identityMatrix[] = {1, 0, 0, 0, 0, 0, 1, 0, 0, 0,
                                       0, 0, 1, 0, 0, 0, 0, 0, 1, 0};

// When aAmount == 0, the identity matrix is returned.
// When aAmount == 1, aToMatrix is returned.
// When aAmount > 1, an exaggerated version of aToMatrix is returned. This can
// be useful in certain cases, such as producing a color matrix to oversaturate
// an image.
//
// This function is a shortcut of a full matrix addition and a scalar multiply,
// and it assumes that the following elements in aToMatrix are 0 and 1:
//   x x x 0 0
//   x x x 0 0
//   x x x 0 0
//   0 0 0 1 0
static void InterpolateFromIdentityMatrix(const float aToMatrix[20],
                                          float aAmount, float aOutMatrix[20]) {
  PodCopy(aOutMatrix, identityMatrix, 20);

  float oneMinusAmount = 1 - aAmount;

  aOutMatrix[0] = aAmount * aToMatrix[0] + oneMinusAmount;
  aOutMatrix[1] = aAmount * aToMatrix[1];
  aOutMatrix[2] = aAmount * aToMatrix[2];

  aOutMatrix[5] = aAmount * aToMatrix[5];
  aOutMatrix[6] = aAmount * aToMatrix[6] + oneMinusAmount;
  aOutMatrix[7] = aAmount * aToMatrix[7];

  aOutMatrix[10] = aAmount * aToMatrix[10];
  aOutMatrix[11] = aAmount * aToMatrix[11];
  aOutMatrix[12] = aAmount * aToMatrix[12] + oneMinusAmount;
}

// Create a 4x5 color matrix for the different ways to specify color matrices
// in SVG.
bool ComputeColorMatrix(const ColorMatrixAttributes& aMatrixAttributes,
                        float aOutMatrix[20]) {
  // Luminance coefficients.
  static const float lumR = 0.2126f;
  static const float lumG = 0.7152f;
  static const float lumB = 0.0722f;

  static const float oneMinusLumR = 1 - lumR;
  static const float oneMinusLumG = 1 - lumG;
  static const float oneMinusLumB = 1 - lumB;

  static const float luminanceToAlphaMatrix[] = {
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, lumR, lumG, lumB, 0, 0};

  static const float saturateMatrix[] = {
      lumR, lumG, lumB, 0, 0, lumR, lumG, lumB, 0, 0,
      lumR, lumG, lumB, 0, 0, 0,    0,    0,    1, 0};

  static const float sepiaMatrix[] = {
      0.393f, 0.769f, 0.189f, 0, 0, 0.349f, 0.686f, 0.168f, 0, 0,
      0.272f, 0.534f, 0.131f, 0, 0, 0,      0,      0,      1, 0};

  // Hue rotate specific coefficients.
  static const float hueRotateR = 0.143f;
  static const float hueRotateG = 0.140f;
  static const float hueRotateB = 0.283f;

  switch (aMatrixAttributes.mType) {
    case SVGFEColorMatrixType::Matrix: {
      if (aMatrixAttributes.mValues.Length() != 20) {
        return false;
      }

      PodCopy(aOutMatrix, aMatrixAttributes.mValues.Elements(), 20);
      break;
    }

    case SVGFEColorMatrixType::Saturate: {
      if (aMatrixAttributes.mValues.Length() != 1) {
        return false;
      }

      float s = aMatrixAttributes.mValues[0];

      if (s < 0) {
        return false;
      }

      InterpolateFromIdentityMatrix(saturateMatrix, 1 - s, aOutMatrix);
      break;
    }

    case SVGFEColorMatrixType::HueRotate: {
      if (aMatrixAttributes.mValues.Length() != 1) {
        return false;
      }

      PodCopy(aOutMatrix, identityMatrix, 20);

      float hueRotateValue = aMatrixAttributes.mValues[0];

      float c = static_cast<float>(cos(hueRotateValue * kRadPerDegree));
      float s = static_cast<float>(sin(hueRotateValue * kRadPerDegree));

      aOutMatrix[0] = lumR + oneMinusLumR * c - lumR * s;
      aOutMatrix[1] = lumG - lumG * c - lumG * s;
      aOutMatrix[2] = lumB - lumB * c + oneMinusLumB * s;

      aOutMatrix[5] = lumR - lumR * c + hueRotateR * s;
      aOutMatrix[6] = lumG + oneMinusLumG * c + hueRotateG * s;
      aOutMatrix[7] = lumB - lumB * c - hueRotateB * s;

      aOutMatrix[10] = lumR - lumR * c - oneMinusLumR * s;
      aOutMatrix[11] = lumG - lumG * c + lumG * s;
      aOutMatrix[12] = lumB + oneMinusLumB * c + lumB * s;

      break;
    }

    case SVGFEColorMatrixType::LuminanceToAlpha: {
      PodCopy(aOutMatrix, luminanceToAlphaMatrix, 20);
      break;
    }

    case SVGFEColorMatrixType::Sepia: {
      if (aMatrixAttributes.mValues.Length() != 1) {
        return false;
      }

      float amount = aMatrixAttributes.mValues[0];

      if (amount < 0 || amount > 1) {
        return false;
      }

      InterpolateFromIdentityMatrix(sepiaMatrix, amount, aOutMatrix);
      break;
    }

    default: {
      return false;
    }
  }

  return !ArrayEqual(aOutMatrix, identityMatrix, 20);
}

static void DisableAllTransfers(FilterNode* aTransferFilterNode) {
  aTransferFilterNode->SetAttribute(ATT_TRANSFER_DISABLE_R, true);
  aTransferFilterNode->SetAttribute(ATT_TRANSFER_DISABLE_G, true);
  aTransferFilterNode->SetAttribute(ATT_TRANSFER_DISABLE_B, true);
  aTransferFilterNode->SetAttribute(ATT_TRANSFER_DISABLE_A, true);
}

// Called for one channel at a time.
// This function creates the required FilterNodes on demand and tries to
// merge conversions of different channels into the same FilterNode if
// possible.
// There's a mismatch between the way SVG and the Moz2D API handle transfer
// functions: In SVG, it's possible to specify a different transfer function
// type for each color channel, but in Moz2D, a given transfer function type
// applies to all color channels.
//
//  @param aFunctionAttributes The attributes of the transfer function for this
//                             channel.
//  @param aChannel The color channel that this function applies to, where
//                  0 = red, 1 = green, 2 = blue, 3 = alpha
//  @param aDT The DrawTarget that the FilterNodes should be created for.
//  @param aTableTransfer Existing FilterNode holders (which may still be
//                        null) that the resulting FilterNodes from this
//                        function will be stored in.
//
static void ConvertComponentTransferFunctionToFilter(
    const ComponentTransferAttributes& aFunctionAttributes, int32_t aInChannel,
    int32_t aOutChannel, DrawTarget* aDT, RefPtr<FilterNode>& aTableTransfer,
    RefPtr<FilterNode>& aDiscreteTransfer, RefPtr<FilterNode>& aLinearTransfer,
    RefPtr<FilterNode>& aGammaTransfer) {
  static const TransferAtts disableAtt[4] = {
      ATT_TRANSFER_DISABLE_R, ATT_TRANSFER_DISABLE_G, ATT_TRANSFER_DISABLE_B,
      ATT_TRANSFER_DISABLE_A};

  RefPtr<FilterNode> filter;

  SVGFEComponentTransferType type = aFunctionAttributes.mTypes[aInChannel];

  switch (type) {
    case SVGFEComponentTransferType::Table: {
      const nsTArray<float>& tableValues =
          aFunctionAttributes.mValues[aInChannel];
      if (tableValues.Length() < 2) return;

      if (!aTableTransfer) {
        aTableTransfer = aDT->CreateFilter(FilterType::TABLE_TRANSFER);
        if (!aTableTransfer) {
          return;
        }
        DisableAllTransfers(aTableTransfer);
      }
      filter = aTableTransfer;
      static const TableTransferAtts tableAtt[4] = {
          ATT_TABLE_TRANSFER_TABLE_R, ATT_TABLE_TRANSFER_TABLE_G,
          ATT_TABLE_TRANSFER_TABLE_B, ATT_TABLE_TRANSFER_TABLE_A};
      filter->SetAttribute(disableAtt[aOutChannel], false);
      filter->SetAttribute(tableAtt[aOutChannel], &tableValues[0],
                           tableValues.Length());
      break;
    }

    case SVGFEComponentTransferType::Discrete: {
      const nsTArray<float>& tableValues =
          aFunctionAttributes.mValues[aInChannel];
      if (tableValues.Length() < 1) return;

      if (!aDiscreteTransfer) {
        aDiscreteTransfer = aDT->CreateFilter(FilterType::DISCRETE_TRANSFER);
        if (!aDiscreteTransfer) {
          return;
        }
        DisableAllTransfers(aDiscreteTransfer);
      }
      filter = aDiscreteTransfer;
      static const DiscreteTransferAtts tableAtt[4] = {
          ATT_DISCRETE_TRANSFER_TABLE_R, ATT_DISCRETE_TRANSFER_TABLE_G,
          ATT_DISCRETE_TRANSFER_TABLE_B, ATT_DISCRETE_TRANSFER_TABLE_A};
      filter->SetAttribute(disableAtt[aOutChannel], false);
      filter->SetAttribute(tableAtt[aOutChannel], &tableValues[0],
                           tableValues.Length());

      break;
    }

    case SVGFEComponentTransferType::Linear: {
      static const LinearTransferAtts slopeAtt[4] = {
          ATT_LINEAR_TRANSFER_SLOPE_R, ATT_LINEAR_TRANSFER_SLOPE_G,
          ATT_LINEAR_TRANSFER_SLOPE_B, ATT_LINEAR_TRANSFER_SLOPE_A};
      static const LinearTransferAtts interceptAtt[4] = {
          ATT_LINEAR_TRANSFER_INTERCEPT_R, ATT_LINEAR_TRANSFER_INTERCEPT_G,
          ATT_LINEAR_TRANSFER_INTERCEPT_B, ATT_LINEAR_TRANSFER_INTERCEPT_A};
      const nsTArray<float>& slopeIntercept =
          aFunctionAttributes.mValues[aInChannel];
      float slope = slopeIntercept[kComponentTransferSlopeIndex];
      float intercept = slopeIntercept[kComponentTransferInterceptIndex];
      if (slope == 1.0f && intercept == 0.0f) {
        return;
      }
      if (!aLinearTransfer) {
        aLinearTransfer = aDT->CreateFilter(FilterType::LINEAR_TRANSFER);
        if (!aLinearTransfer) {
          return;
        }
        DisableAllTransfers(aLinearTransfer);
      }
      filter = aLinearTransfer;
      filter->SetAttribute(disableAtt[aOutChannel], false);
      filter->SetAttribute(slopeAtt[aOutChannel], slope);
      filter->SetAttribute(interceptAtt[aOutChannel], intercept);
      break;
    }

    case SVGFEComponentTransferType::Gamma: {
      static const GammaTransferAtts amplitudeAtt[4] = {
          ATT_GAMMA_TRANSFER_AMPLITUDE_R, ATT_GAMMA_TRANSFER_AMPLITUDE_G,
          ATT_GAMMA_TRANSFER_AMPLITUDE_B, ATT_GAMMA_TRANSFER_AMPLITUDE_A};
      static const GammaTransferAtts exponentAtt[4] = {
          ATT_GAMMA_TRANSFER_EXPONENT_R, ATT_GAMMA_TRANSFER_EXPONENT_G,
          ATT_GAMMA_TRANSFER_EXPONENT_B, ATT_GAMMA_TRANSFER_EXPONENT_A};
      static const GammaTransferAtts offsetAtt[4] = {
          ATT_GAMMA_TRANSFER_OFFSET_R, ATT_GAMMA_TRANSFER_OFFSET_G,
          ATT_GAMMA_TRANSFER_OFFSET_B, ATT_GAMMA_TRANSFER_OFFSET_A};
      if (!aGammaTransfer) {
        aGammaTransfer = aDT->CreateFilter(FilterType::GAMMA_TRANSFER);
        if (!aGammaTransfer) {
          return;
        }
        DisableAllTransfers(aGammaTransfer);
      }
      filter = aGammaTransfer;
      filter->SetAttribute(disableAtt[aOutChannel], false);
      const nsTArray<float>& gammaValues =
          aFunctionAttributes.mValues[aInChannel];
      float amplitude = gammaValues[kComponentTransferAmplitudeIndex];
      float exponent = gammaValues[kComponentTransferExponentIndex];
      float offset = gammaValues[kComponentTransferOffsetIndex];
      filter->SetAttribute(amplitudeAtt[aOutChannel], amplitude);
      filter->SetAttribute(exponentAtt[aOutChannel], exponent);
      filter->SetAttribute(offsetAtt[aOutChannel], offset);
      break;
    }

    case SVGFEComponentTransferType::Identity:
    default:
      break;
  }
}

const int32_t kMorphologyMaxRadius = 100000;

// Handle the different primitive description types and create the necessary
// FilterNode(s) for each.
// Returns nullptr for invalid filter primitives. This should be interpreted as
// transparent black by the caller.
// aSourceRegions contains the filter primitive subregions of the source
// primitives; only needed for eTile primitives.
// aInputImages carries additional surfaces that are used by eImage primitives.
static already_AddRefed<FilterNode> FilterNodeFromPrimitiveDescription(
    const FilterPrimitiveDescription& aDescription, DrawTarget* aDT,
    nsTArray<RefPtr<FilterNode>>& aSources, nsTArray<IntRect>& aSourceRegions,
    nsTArray<RefPtr<SourceSurface>>& aInputImages) {
  struct PrimitiveAttributesMatcher {
    PrimitiveAttributesMatcher(const FilterPrimitiveDescription& aDescription,
                               DrawTarget* aDT,
                               nsTArray<RefPtr<FilterNode>>& aSources,
                               nsTArray<IntRect>& aSourceRegions,
                               nsTArray<RefPtr<SourceSurface>>& aInputImages)
        : mDescription(aDescription),
          mDT(aDT),
          mSources(aSources),
          mSourceRegions(aSourceRegions),
          mInputImages(aInputImages) {}

    const FilterPrimitiveDescription& mDescription;
    DrawTarget* mDT;
    nsTArray<RefPtr<FilterNode>>& mSources;
    nsTArray<IntRect>& mSourceRegions;
    nsTArray<RefPtr<SourceSurface>>& mInputImages;

    already_AddRefed<FilterNode> operator()(
        const EmptyAttributes& aEmptyAttributes) {
      return nullptr;
    }

    already_AddRefed<FilterNode> operator()(const BlendAttributes& aBlend) {
      SVGFEBlendMode mode = aBlend.mBlendMode;
      RefPtr<FilterNode> filter;
      if (mode == SVGFEBlendMode::Unknown) {
        return nullptr;
      }
      if (mode == SVGFEBlendMode::Normal) {
        filter = mDT->CreateFilter(FilterType::COMPOSITE);
        if (!filter) {
          return nullptr;
        }
        filter->SetInput(IN_COMPOSITE_IN_START, mSources[1]);
        filter->SetInput(IN_COMPOSITE_IN_START + 1, mSources[0]);
      } else {
        filter = mDT->CreateFilter(FilterType::BLEND);
        if (!filter) {
          return nullptr;
        }
        static const uint8_t blendModes[] = {0,
                                             0,
                                             BLEND_MODE_MULTIPLY,
                                             BLEND_MODE_SCREEN,
                                             BLEND_MODE_DARKEN,
                                             BLEND_MODE_LIGHTEN,
                                             BLEND_MODE_OVERLAY,
                                             BLEND_MODE_COLOR_DODGE,
                                             BLEND_MODE_COLOR_BURN,
                                             BLEND_MODE_HARD_LIGHT,
                                             BLEND_MODE_SOFT_LIGHT,
                                             BLEND_MODE_DIFFERENCE,
                                             BLEND_MODE_EXCLUSION,
                                             BLEND_MODE_HUE,
                                             BLEND_MODE_SATURATION,
                                             BLEND_MODE_COLOR,
                                             BLEND_MODE_LUMINOSITY};
        static_assert(std::size(blendModes) ==
                      ContiguousEnumSize<SVGFEBlendMode>::value);
        filter->SetAttribute(ATT_BLEND_BLENDMODE,
                             (uint32_t)blendModes[static_cast<uint8_t>(mode)]);
        // The correct input order for both software and D2D filters is flipped
        // from our source order, so flip here.
        filter->SetInput(IN_BLEND_IN, mSources[1]);
        filter->SetInput(IN_BLEND_IN2, mSources[0]);
      }
      return filter.forget();
    }

    already_AddRefed<FilterNode> operator()(
        const ColorMatrixAttributes& aMatrixAttributes) {
      float colorMatrix[20];
      if (!ComputeColorMatrix(aMatrixAttributes, colorMatrix)) {
        RefPtr<FilterNode> filter(mSources[0]);
        return filter.forget();
      }

      Matrix5x4 matrix(
          colorMatrix[0], colorMatrix[5], colorMatrix[10], colorMatrix[15],
          colorMatrix[1], colorMatrix[6], colorMatrix[11], colorMatrix[16],
          colorMatrix[2], colorMatrix[7], colorMatrix[12], colorMatrix[17],
          colorMatrix[3], colorMatrix[8], colorMatrix[13], colorMatrix[18],
          colorMatrix[4], colorMatrix[9], colorMatrix[14], colorMatrix[19]);

      RefPtr<FilterNode> filter = mDT->CreateFilter(FilterType::COLOR_MATRIX);
      if (!filter) {
        return nullptr;
      }
      filter->SetAttribute(ATT_COLOR_MATRIX_MATRIX, matrix);
      filter->SetAttribute(ATT_COLOR_MATRIX_ALPHA_MODE,
                           (uint32_t)ALPHA_MODE_STRAIGHT);
      filter->SetInput(IN_COLOR_MATRIX_IN, mSources[0]);
      return filter.forget();
    }

    already_AddRefed<FilterNode> operator()(
        const MorphologyAttributes& aMorphology) {
      Size radii = aMorphology.mRadii;
      int32_t rx = radii.width;
      int32_t ry = radii.height;

      // Are both of the radii zero or negative, return the input image
      if (rx <= 0 && ry <= 0) {
        RefPtr<FilterNode> filter(mSources[0]);
        return filter.forget();
      }

      // Clamp radii to prevent completely insane values:
      rx = std::clamp(rx, 0, kMorphologyMaxRadius);
      ry = std::clamp(ry, 0, kMorphologyMaxRadius);

      MorphologyOperator op =
          aMorphology.mOperator == SVGMorphologyOperator::Erode
              ? MORPHOLOGY_OPERATOR_ERODE
              : MORPHOLOGY_OPERATOR_DILATE;

      RefPtr<FilterNode> filter = mDT->CreateFilter(FilterType::MORPHOLOGY);
      if (!filter) {
        return nullptr;
      }
      filter->SetAttribute(ATT_MORPHOLOGY_RADII, IntSize(rx, ry));
      filter->SetAttribute(ATT_MORPHOLOGY_OPERATOR, (uint32_t)op);
      filter->SetInput(IN_MORPHOLOGY_IN, mSources[0]);
      return filter.forget();
    }

    already_AddRefed<FilterNode> operator()(const FloodAttributes& aFlood) {
      RefPtr<FilterNode> filter = mDT->CreateFilter(FilterType::FLOOD);
      if (!filter) {
        return nullptr;
      }
      filter->SetAttribute(ATT_FLOOD_COLOR, ToDeviceColor(aFlood.mColor));
      return filter.forget();
    }

    already_AddRefed<FilterNode> operator()(const TileAttributes& aTile) {
      RefPtr<FilterNode> filter = mDT->CreateFilter(FilterType::TILE);
      if (!filter) {
        return nullptr;
      }
      filter->SetAttribute(ATT_TILE_SOURCE_RECT, mSourceRegions[0]);
      filter->SetInput(IN_TILE_IN, mSources[0]);
      return filter.forget();
    }

    already_AddRefed<FilterNode> operator()(
        const ComponentTransferAttributes& aComponentTransfer) {
      MOZ_ASSERT(aComponentTransfer.mTypes[0] !=
                 SVGFEComponentTransferType::SameAsR);
      MOZ_ASSERT(aComponentTransfer.mTypes[3] !=
                 SVGFEComponentTransferType::SameAsR);

      RefPtr<FilterNode> filters[std::size(aComponentTransfer.mTypes)];
      for (size_t i = 0; i < std::size(aComponentTransfer.mTypes); i++) {
        int32_t inputIndex = (aComponentTransfer.mTypes[i] ==
                              SVGFEComponentTransferType::SameAsR) &&
                                     (i < 3)
                                 ? 0
                                 : i;
        ConvertComponentTransferFunctionToFilter(aComponentTransfer, inputIndex,
                                                 i, mDT, filters[0], filters[1],
                                                 filters[2], filters[3]);
      }

      // Connect all used filters nodes.
      RefPtr<FilterNode> lastFilter = mSources[0];
      for (size_t i = 0; i < std::size(aComponentTransfer.mTypes); i++) {
        if (filters[i]) {
          filters[i]->SetInput(0, lastFilter);
          lastFilter = filters[i];
        }
      }

      return lastFilter.forget();
    }

    already_AddRefed<FilterNode> operator()(const OpacityAttributes& aOpacity) {
      if (aOpacity.mOpacity == 1.0f) {
        RefPtr<FilterNode> filter(mSources[0]);
        return filter.forget();
      }
      if (aOpacity.mOpacity == 0.0f) {
        return nullptr;
      }
      RefPtr<FilterNode> filter = mDT->CreateFilter(FilterType::OPACITY);
      if (!filter) {
        return nullptr;
      }
      filter->SetAttribute(ATT_OPACITY_VALUE, aOpacity.mOpacity);
      filter->SetInput(IN_OPACITY_IN, mSources[0]);
      return filter.forget();
    }

    already_AddRefed<FilterNode> operator()(
        const ConvolveMatrixAttributes& aConvolveMatrix) {
      RefPtr<FilterNode> filter =
          mDT->CreateFilter(FilterType::CONVOLVE_MATRIX);
      if (!filter) {
        return nullptr;
      }
      filter->SetAttribute(ATT_CONVOLVE_MATRIX_KERNEL_SIZE,
                           aConvolveMatrix.mKernelSize);
      const nsTArray<float>& matrix = aConvolveMatrix.mKernelMatrix;
      filter->SetAttribute(ATT_CONVOLVE_MATRIX_KERNEL_MATRIX, matrix.Elements(),
                           matrix.Length());
      filter->SetAttribute(ATT_CONVOLVE_MATRIX_DIVISOR,
                           aConvolveMatrix.mDivisor);
      filter->SetAttribute(ATT_CONVOLVE_MATRIX_BIAS, aConvolveMatrix.mBias);
      filter->SetAttribute(ATT_CONVOLVE_MATRIX_TARGET, aConvolveMatrix.mTarget);
      filter->SetAttribute(ATT_CONVOLVE_MATRIX_RENDER_RECT,
                           mDescription.PrimitiveSubregion());
      static const uint8_t edgeModes[] = {
          EDGE_MODE_NONE,       // SVGEdgeMode::Unknown
          EDGE_MODE_DUPLICATE,  // SVGEdgeMode::Duplicate
          EDGE_MODE_WRAP,       // SVGEdgeMode::Wrap
          EDGE_MODE_NONE        // SVGEdgeMode::None
      };
      static_assert(std::size(edgeModes) ==
                    ContiguousEnumSize<SVGEdgeMode>::value);
      filter->SetAttribute(
          ATT_CONVOLVE_MATRIX_EDGE_MODE,
          (uint32_t)edgeModes[static_cast<uint8_t>(aConvolveMatrix.mEdgeMode)]);
      filter->SetAttribute(ATT_CONVOLVE_MATRIX_KERNEL_UNIT_LENGTH,
                           aConvolveMatrix.mKernelUnitLength);
      filter->SetAttribute(ATT_CONVOLVE_MATRIX_PRESERVE_ALPHA,
                           aConvolveMatrix.mPreserveAlpha);
      filter->SetInput(IN_CONVOLVE_MATRIX_IN, mSources[0]);
      return filter.forget();
    }

    already_AddRefed<FilterNode> operator()(const OffsetAttributes& aOffset) {
      return FilterWrappers::Offset(mDT, mSources[0], aOffset.mValue);
    }

    already_AddRefed<FilterNode> operator()(
        const DisplacementMapAttributes& aDisplacementMap) {
      RefPtr<FilterNode> filter =
          mDT->CreateFilter(FilterType::DISPLACEMENT_MAP);
      if (!filter) {
        return nullptr;
      }
      filter->SetAttribute(ATT_DISPLACEMENT_MAP_SCALE, aDisplacementMap.mScale);
      static const uint8_t channel[] = {
          COLOR_CHANNEL_R,  // SVGChannel::Unknown
          COLOR_CHANNEL_R,  // SVGChannel::R
          COLOR_CHANNEL_G,  // SVGChannel::G
          COLOR_CHANNEL_B,  // SVGChannel::B
          COLOR_CHANNEL_A   // SVGChannel::A
      };
      static_assert(std::size(channel) ==
                    ContiguousEnumSize<SVGChannel>::value);
      filter->SetAttribute(
          ATT_DISPLACEMENT_MAP_X_CHANNEL,
          (uint32_t)channel[static_cast<uint8_t>(aDisplacementMap.mXChannel)]);
      filter->SetAttribute(
          ATT_DISPLACEMENT_MAP_Y_CHANNEL,
          (uint32_t)channel[static_cast<uint8_t>(aDisplacementMap.mYChannel)]);
      filter->SetInput(IN_DISPLACEMENT_MAP_IN, mSources[0]);
      filter->SetInput(IN_DISPLACEMENT_MAP_IN2, mSources[1]);
      return filter.forget();
    }

    already_AddRefed<FilterNode> operator()(
        const TurbulenceAttributes& aTurbulence) {
      RefPtr<FilterNode> filter = mDT->CreateFilter(FilterType::TURBULENCE);
      if (!filter) {
        return nullptr;
      }
      filter->SetAttribute(ATT_TURBULENCE_BASE_FREQUENCY,
                           aTurbulence.mBaseFrequency);
      filter->SetAttribute(ATT_TURBULENCE_NUM_OCTAVES, aTurbulence.mOctaves);
      filter->SetAttribute(ATT_TURBULENCE_STITCHABLE, aTurbulence.mStitchable);
      filter->SetAttribute(ATT_TURBULENCE_SEED, (uint32_t)aTurbulence.mSeed);
      static const uint8_t type[] = {
          TURBULENCE_TYPE_FRACTAL_NOISE,  // SVGTurbulenceType::Unknown
          TURBULENCE_TYPE_FRACTAL_NOISE,  // SVGTurbulenceType::FractalNoise
          TURBULENCE_TYPE_TURBULENCE      // SVGTurbulenceType::Turbulence
      };
      static_assert(std::size(type) ==
                    ContiguousEnumSize<SVGTurbulenceType>::value);
      filter->SetAttribute(
          ATT_TURBULENCE_TYPE,
          (uint32_t)type[static_cast<uint8_t>(aTurbulence.mType)]);
      filter->SetAttribute(
          ATT_TURBULENCE_RECT,
          mDescription.PrimitiveSubregion() - aTurbulence.mOffset);
      return FilterWrappers::Offset(mDT, filter, aTurbulence.mOffset);
    }

    already_AddRefed<FilterNode> operator()(
        const CompositeAttributes& aComposite) {
      RefPtr<FilterNode> filter;
      SVGFECompositeOperator op = aComposite.mOperator;
      if (op == SVGFECompositeOperator::Arithmetic) {
        const nsTArray<float>& coefficients = aComposite.mCoefficients;
        static const float allZero[4] = {0, 0, 0, 0};
        filter = mDT->CreateFilter(FilterType::ARITHMETIC_COMBINE);
        // All-zero coefficients sometimes occur in junk filters.
        if (!filter || (coefficients.Length() == std::size(allZero) &&
                        ArrayEqual(coefficients.Elements(), allZero,
                                   std::size(allZero)))) {
          return nullptr;
        }
        filter->SetAttribute(ATT_ARITHMETIC_COMBINE_COEFFICIENTS,
                             coefficients.Elements(), coefficients.Length());
        filter->SetInput(IN_ARITHMETIC_COMBINE_IN, mSources[0]);
        filter->SetInput(IN_ARITHMETIC_COMBINE_IN2, mSources[1]);
      } else {
        filter = mDT->CreateFilter(FilterType::COMPOSITE);
        if (!filter) {
          return nullptr;
        }
        static const uint8_t operators[] = {
            COMPOSITE_OPERATOR_OVER,    // SVGFECompositeOperator::Unknown
            COMPOSITE_OPERATOR_OVER,    // SVGFECompositeOperator::Over
            COMPOSITE_OPERATOR_IN,      // SVGFECompositeOperator::In
            COMPOSITE_OPERATOR_OUT,     // SVGFECompositeOperator::Out
            COMPOSITE_OPERATOR_ATOP,    // SVGFECompositeOperator::Atop
            COMPOSITE_OPERATOR_XOR,     // SVGFECompositeOperator::Xor
            COMPOSITE_OPERATOR_OVER,    // Unused, arithmetic is handled above
            COMPOSITE_OPERATOR_LIGHTER  // SVGFECompositeOperator::Lighter
        };
        static_assert(std::size(operators) ==
                      ContiguousEnumSize<SVGFECompositeOperator>::value);
        filter->SetAttribute(ATT_COMPOSITE_OPERATOR,
                             (uint32_t)operators[static_cast<uint8_t>(op)]);
        filter->SetInput(IN_COMPOSITE_IN_START, mSources[1]);
        filter->SetInput(IN_COMPOSITE_IN_START + 1, mSources[0]);
      }
      return filter.forget();
    }

    already_AddRefed<FilterNode> operator()(const MergeAttributes& aMerge) {
      if (mSources.Length() == 0) {
        return nullptr;
      }
      if (mSources.Length() == 1) {
        RefPtr<FilterNode> filter(mSources[0]);
        return filter.forget();
      }
      RefPtr<FilterNode> filter = mDT->CreateFilter(FilterType::COMPOSITE);
      if (!filter) {
        return nullptr;
      }
      filter->SetAttribute(ATT_COMPOSITE_OPERATOR,
                           (uint32_t)COMPOSITE_OPERATOR_OVER);
      for (size_t i = 0; i < mSources.Length(); i++) {
        filter->SetInput(IN_COMPOSITE_IN_START + i, mSources[i]);
      }
      return filter.forget();
    }

    already_AddRefed<FilterNode> operator()(
        const GaussianBlurAttributes& aGaussianBlur) {
      return FilterWrappers::GaussianBlur(mDT, mSources[0],
                                          aGaussianBlur.mStdDeviation);
    }

    already_AddRefed<FilterNode> operator()(
        const DropShadowAttributes& aDropShadow) {
      RefPtr<FilterNode> alpha = FilterWrappers::ToAlpha(mDT, mSources[0]);
      RefPtr<FilterNode> blur =
          FilterWrappers::GaussianBlur(mDT, alpha, aDropShadow.mStdDeviation);
      RefPtr<FilterNode> offsetBlur = FilterWrappers::Offset(
          mDT, blur, IntPoint::Truncate(aDropShadow.mOffset));
      RefPtr<FilterNode> flood = mDT->CreateFilter(FilterType::FLOOD);
      if (!flood) {
        return nullptr;
      }
      sRGBColor color = aDropShadow.mColor;
      if (mDescription.InputColorSpace(0) == ColorSpace::LinearRGB) {
        // We use the colour space we will need as input to the next
        // filter rather than convert the whole region after the flood.
        color = FilterWrappers::SRGBToLinearRGB(color);
      }
      flood->SetAttribute(ATT_FLOOD_COLOR, ToDeviceColor(color));

      RefPtr<FilterNode> composite = mDT->CreateFilter(FilterType::COMPOSITE);
      if (!composite) {
        return nullptr;
      }
      composite->SetAttribute(ATT_COMPOSITE_OPERATOR,
                              (uint32_t)COMPOSITE_OPERATOR_IN);
      composite->SetInput(IN_COMPOSITE_IN_START, offsetBlur);
      composite->SetInput(IN_COMPOSITE_IN_START + 1, flood);

      RefPtr<FilterNode> filter = mDT->CreateFilter(FilterType::COMPOSITE);
      if (!filter) {
        return nullptr;
      }
      filter->SetAttribute(ATT_COMPOSITE_OPERATOR,
                           (uint32_t)COMPOSITE_OPERATOR_OVER);
      filter->SetInput(IN_COMPOSITE_IN_START, composite);
      filter->SetInput(IN_COMPOSITE_IN_START + 1, mSources[0]);
      return filter.forget();
    }

    already_AddRefed<FilterNode> operator()(
        const LightingAttributes& aLighting) {
      bool isSpecular =
          mDescription.Attributes().is<SpecularLightingAttributes>();

      if (aLighting.mLightType == LightType::None) {
        return nullptr;
      }

      enum { POINT = 0, SPOT, DISTANT } lightType = POINT;

      switch (aLighting.mLightType) {
        case LightType::Point:
          lightType = POINT;
          break;
        case LightType::Spot:
          lightType = SPOT;
          break;
        case LightType::Distant:
          lightType = DISTANT;
          break;
        default:
          break;
      }

      static const FilterType filterType[2][DISTANT + 1] = {
          {FilterType::POINT_DIFFUSE, FilterType::SPOT_DIFFUSE,
           FilterType::DISTANT_DIFFUSE},
          {FilterType::POINT_SPECULAR, FilterType::SPOT_SPECULAR,
           FilterType::DISTANT_SPECULAR}};
      RefPtr<FilterNode> filter =
          mDT->CreateFilter(filterType[isSpecular][lightType]);
      if (!filter) {
        return nullptr;
      }

      filter->SetAttribute(ATT_LIGHTING_COLOR, ToDeviceColor(aLighting.mColor));
      filter->SetAttribute(ATT_LIGHTING_SURFACE_SCALE, aLighting.mSurfaceScale);
      filter->SetAttribute(ATT_LIGHTING_KERNEL_UNIT_LENGTH,
                           aLighting.mKernelUnitLength);
      filter->SetAttribute(ATT_LIGHTING_RENDER_RECT,
                           mDescription.PrimitiveSubregion());

      if (isSpecular) {
        filter->SetAttribute(ATT_SPECULAR_LIGHTING_SPECULAR_CONSTANT,
                             aLighting.mLightingConstant);
        filter->SetAttribute(ATT_SPECULAR_LIGHTING_SPECULAR_EXPONENT,
                             aLighting.mSpecularExponent);
      } else {
        filter->SetAttribute(ATT_DIFFUSE_LIGHTING_DIFFUSE_CONSTANT,
                             aLighting.mLightingConstant);
      }

      switch (lightType) {
        case POINT: {
          Point3D position(aLighting.mLightValues[kPointLightPositionXIndex],
                           aLighting.mLightValues[kPointLightPositionYIndex],
                           aLighting.mLightValues[kPointLightPositionZIndex]);
          filter->SetAttribute(ATT_POINT_LIGHT_POSITION, position);
          break;
        }
        case SPOT: {
          Point3D position(aLighting.mLightValues[kSpotLightPositionXIndex],
                           aLighting.mLightValues[kSpotLightPositionYIndex],
                           aLighting.mLightValues[kSpotLightPositionZIndex]);
          filter->SetAttribute(ATT_SPOT_LIGHT_POSITION, position);
          Point3D pointsAt(aLighting.mLightValues[kSpotLightPointsAtXIndex],
                           aLighting.mLightValues[kSpotLightPointsAtYIndex],
                           aLighting.mLightValues[kSpotLightPointsAtZIndex]);
          filter->SetAttribute(ATT_SPOT_LIGHT_POINTS_AT, pointsAt);
          filter->SetAttribute(ATT_SPOT_LIGHT_FOCUS,
                               aLighting.mLightValues[kSpotLightFocusIndex]);
          filter->SetAttribute(
              ATT_SPOT_LIGHT_LIMITING_CONE_ANGLE,
              aLighting.mLightValues[kSpotLightLimitingConeAngleIndex]);
          break;
        }
        case DISTANT: {
          filter->SetAttribute(
              ATT_DISTANT_LIGHT_AZIMUTH,
              aLighting.mLightValues[kDistantLightAzimuthIndex]);
          filter->SetAttribute(
              ATT_DISTANT_LIGHT_ELEVATION,
              aLighting.mLightValues[kDistantLightElevationIndex]);
          break;
        }
      }

      filter->SetInput(IN_LIGHTING_IN, mSources[0]);

      return filter.forget();
    }

    already_AddRefed<FilterNode> operator()(const ImageAttributes& aImage) {
      const Matrix& TM = aImage.mTransform;
      if (!TM.Determinant()) {
        return nullptr;
      }

      // Pull the image from the additional image list using the index that's
      // stored in the primitive description.
      RefPtr<SourceSurface> inputImage = mInputImages[aImage.mInputIndex];

      RefPtr<FilterNode> transform = mDT->CreateFilter(FilterType::TRANSFORM);
      if (!transform) {
        return nullptr;
      }
      transform->SetInput(IN_TRANSFORM_IN, inputImage);
      transform->SetAttribute(ATT_TRANSFORM_MATRIX, TM);
      transform->SetAttribute(ATT_TRANSFORM_FILTER, aImage.mFilter);
      return transform.forget();
    }

    already_AddRefed<FilterNode> operator()(const ToAlphaAttributes& aToAlpha) {
      return FilterWrappers::ToAlpha(mDT, mSources[0]);
    }
  };

  return aDescription.Attributes().match(PrimitiveAttributesMatcher(
      aDescription, aDT, aSources, aSourceRegions, aInputImages));
}

template <typename T>
static const T& ElementForIndex(int32_t aIndex,
                                const nsTArray<T>& aPrimitiveElements,
                                const T& aSourceGraphicElement,
                                const T& aFillPaintElement,
                                const T& aStrokePaintElement) {
  switch (aIndex) {
    case FilterPrimitiveDescription::kPrimitiveIndexSourceGraphic:
    case FilterPrimitiveDescription::kPrimitiveIndexSourceAlpha:
      return aSourceGraphicElement;
    case FilterPrimitiveDescription::kPrimitiveIndexFillPaint:
      return aFillPaintElement;
    case FilterPrimitiveDescription::kPrimitiveIndexStrokePaint:
      return aStrokePaintElement;
    default:
      MOZ_ASSERT(aIndex >= 0, "bad index");
      return aPrimitiveElements[aIndex];
  }
}

static AlphaModel InputAlphaModelForPrimitive(
    const FilterPrimitiveDescription& aDescr, int32_t aInputIndex,
    AlphaModel aOriginalAlphaModel) {
  const PrimitiveAttributes& atts = aDescr.Attributes();
  if (atts.is<TileAttributes>() || atts.is<OffsetAttributes>() ||
      atts.is<ToAlphaAttributes>()) {
    return aOriginalAlphaModel;
  }
  if (atts.is<ColorMatrixAttributes>() ||
      atts.is<ComponentTransferAttributes>()) {
    return AlphaModel::Unpremultiplied;
  }
  if (atts.is<DisplacementMapAttributes>()) {
    return aInputIndex == 0 ? AlphaModel::Premultiplied
                            : AlphaModel::Unpremultiplied;
  }
  if (atts.is<ConvolveMatrixAttributes>()) {
    return atts.as<ConvolveMatrixAttributes>().mPreserveAlpha
               ? AlphaModel::Unpremultiplied
               : AlphaModel::Premultiplied;
  }
  return AlphaModel::Premultiplied;
}

static AlphaModel OutputAlphaModelForPrimitive(
    const FilterPrimitiveDescription& aDescr,
    const nsTArray<AlphaModel>& aInputAlphaModels) {
  if (aInputAlphaModels.Length()) {
    // For filters with inputs, the output is premultiplied if and only if the
    // first input is premultiplied.
    return InputAlphaModelForPrimitive(aDescr, 0, aInputAlphaModels[0]);
  }

  // All filters without inputs produce premultiplied alpha.
  return AlphaModel::Premultiplied;
}

// Returns the output FilterNode, in premultiplied sRGB space.
already_AddRefed<FilterNode> FilterNodeGraphFromDescription(
    DrawTarget* aDT, const FilterDescription& aFilter,
    const Rect& aResultNeededRect, FilterNode* aSourceGraphic,
    const IntRect& aSourceGraphicRect, FilterNode* aFillPaint,
    FilterNode* aStrokePaint,
    nsTArray<RefPtr<SourceSurface>>& aAdditionalImages) {
  const nsTArray<FilterPrimitiveDescription>& primitives = aFilter.mPrimitives;
  MOZ_RELEASE_ASSERT(!primitives.IsEmpty());

  RefPtr<FilterCachedColorModels> sourceFilters[4];
  nsTArray<RefPtr<FilterCachedColorModels>> primitiveFilters;

  for (const auto& descr : primitives) {
    nsTArray<RefPtr<FilterNode>> inputFilterNodes;
    nsTArray<IntRect> inputSourceRects;
    nsTArray<AlphaModel> inputAlphaModels;

    for (size_t j = 0; j < descr.NumberOfInputs(); j++) {
      int32_t inputIndex = descr.InputPrimitiveIndex(j);
      if (inputIndex < 0) {
        inputSourceRects.AppendElement(descr.FilterSpaceBounds());
      } else {
        inputSourceRects.AppendElement(
            primitives[inputIndex].PrimitiveSubregion());
      }

      RefPtr<FilterCachedColorModels> inputFilter;
      if (inputIndex >= 0) {
        MOZ_ASSERT(inputIndex < (int64_t)primitiveFilters.Length(),
                   "out-of-bounds input index!");
        inputFilter = primitiveFilters[inputIndex];
        MOZ_ASSERT(
            inputFilter,
            "Referred to input filter that comes after the current one?");
      } else {
        int32_t sourceIndex = -inputIndex - 1;
        MOZ_ASSERT(sourceIndex >= 0, "invalid source index");
        MOZ_ASSERT(sourceIndex < 4, "invalid source index");
        inputFilter = sourceFilters[sourceIndex];
        if (!inputFilter) {
          RefPtr<FilterNode> sourceFilterNode;

          nsTArray<FilterNode*> primitiveFilters;
          RefPtr<FilterNode> filt =
              ElementForIndex(inputIndex, primitiveFilters, aSourceGraphic,
                              aFillPaint, aStrokePaint);
          if (filt) {
            sourceFilterNode = filt;

            // Clip the original SourceGraphic to the first filter region if the
            // surface isn't already sized appropriately.
            if ((inputIndex ==
                     FilterPrimitiveDescription::kPrimitiveIndexSourceGraphic ||
                 inputIndex ==
                     FilterPrimitiveDescription::kPrimitiveIndexSourceAlpha) &&
                !descr.FilterSpaceBounds().Contains(aSourceGraphicRect)) {
              sourceFilterNode = FilterWrappers::Crop(
                  aDT, sourceFilterNode, descr.FilterSpaceBounds());
            }

            if (inputIndex ==
                FilterPrimitiveDescription::kPrimitiveIndexSourceAlpha) {
              sourceFilterNode = FilterWrappers::ToAlpha(aDT, sourceFilterNode);
            }
          }

          inputFilter = new FilterCachedColorModels(aDT, sourceFilterNode,
                                                    ColorModel::PremulSRGB());
          sourceFilters[sourceIndex] = inputFilter;
        }
      }
      MOZ_ASSERT(inputFilter);

      AlphaModel inputAlphaModel = InputAlphaModelForPrimitive(
          descr, j, inputFilter->OriginalAlphaModel());
      inputAlphaModels.AppendElement(inputAlphaModel);
      ColorModel inputColorModel(descr.InputColorSpace(j), inputAlphaModel);
      inputFilterNodes.AppendElement(
          inputFilter->ForColorModel(inputColorModel));
    }

    RefPtr<FilterNode> primitiveFilterNode = FilterNodeFromPrimitiveDescription(
        descr, aDT, inputFilterNodes, inputSourceRects, aAdditionalImages);

    if (primitiveFilterNode) {
      primitiveFilterNode = FilterWrappers::Crop(aDT, primitiveFilterNode,
                                                 descr.PrimitiveSubregion());
    }

    ColorModel outputColorModel(
        descr.OutputColorSpace(),
        OutputAlphaModelForPrimitive(descr, inputAlphaModels));
    RefPtr<FilterCachedColorModels> primitiveFilter =
        new FilterCachedColorModels(aDT, primitiveFilterNode, outputColorModel);

    primitiveFilters.AppendElement(primitiveFilter);
  }

  MOZ_RELEASE_ASSERT(!primitiveFilters.IsEmpty());
  return primitiveFilters.LastElement()->ForColorModel(
      ColorModel::PremulSRGB());
}

// FilterSupport

void FilterSupport::RenderFilterDescription(
    DrawTarget* aDT, const FilterDescription& aFilter, const Rect& aRenderRect,
    RefPtr<FilterNode> aSourceGraphic, const IntRect& aSourceGraphicRect,
    RefPtr<FilterNode> aFillPaint, const IntRect& aFillPaintRect,
    RefPtr<FilterNode> aStrokePaint, const IntRect& aStrokePaintRect,
    nsTArray<RefPtr<SourceSurface>>& aAdditionalImages, const Point& aDestPoint,
    const DrawOptions& aOptions) {
  RefPtr<FilterNode> resultFilter = FilterNodeGraphFromDescription(
      aDT, aFilter, aRenderRect, aSourceGraphic, aSourceGraphicRect, aFillPaint,
      aStrokePaint, aAdditionalImages);
  if (!resultFilter) {
    gfxWarning() << "Filter is NULL.";
    return;
  }
  aDT->DrawFilter(resultFilter, aRenderRect, aDestPoint, aOptions);
}

static nsIntRegion UnionOfRegions(const nsTArray<nsIntRegion>& aRegions) {
  nsIntRegion result;
  for (const auto& region : aRegions) {
    result.OrWith(region);
  }
  return result;
}

static int32_t InflateSizeForBlurStdDev(float aStdDev) {
  double size =
      std::min(aStdDev, kMaxStdDeviation) * (3 * sqrt(2 * M_PI) / 4) * 1.5;
  return uint32_t(floor(size + 0.5));
}

static nsIntRegion ResultChangeRegionForPrimitive(
    const FilterPrimitiveDescription& aDescription,
    const nsTArray<nsIntRegion>& aInputChangeRegions) {
  struct PrimitiveAttributesMatcher {
    PrimitiveAttributesMatcher(const FilterPrimitiveDescription& aDescription,
                               const nsTArray<nsIntRegion>& aInputChangeRegions)
        : mDescription(aDescription),
          mInputChangeRegions(aInputChangeRegions) {}

    const FilterPrimitiveDescription& mDescription;
    const nsTArray<nsIntRegion>& mInputChangeRegions;

    nsIntRegion operator()(const EmptyAttributes& aEmptyAttributes) {
      return nsIntRegion();
    }

    nsIntRegion operator()(const BlendAttributes& aBlend) {
      return UnionOfRegions(mInputChangeRegions);
    }

    nsIntRegion operator()(const ColorMatrixAttributes& aColorMatrix) {
      return mInputChangeRegions[0];
    }

    nsIntRegion operator()(const MorphologyAttributes& aMorphology) {
      Size radii = aMorphology.mRadii;
      int32_t rx =
          std::clamp(int32_t(ceil(radii.width)), 0, kMorphologyMaxRadius);
      int32_t ry =
          std::clamp(int32_t(ceil(radii.height)), 0, kMorphologyMaxRadius);
      return mInputChangeRegions[0].Inflated(nsIntMargin(ry, rx, ry, rx));
    }

    nsIntRegion operator()(const FloodAttributes& aFlood) {
      return nsIntRegion();
    }

    nsIntRegion operator()(const TileAttributes& aTile) {
      return mDescription.PrimitiveSubregion();
    }

    nsIntRegion operator()(
        const ComponentTransferAttributes& aComponentTransfer) {
      return mInputChangeRegions[0];
    }

    nsIntRegion operator()(const OpacityAttributes& aOpacity) {
      return UnionOfRegions(mInputChangeRegions);
    }

    nsIntRegion operator()(const ConvolveMatrixAttributes& aConvolveMatrix) {
      if (aConvolveMatrix.mEdgeMode != SVGEdgeMode::None) {
        return mDescription.PrimitiveSubregion();
      }
      Size kernelUnitLength = aConvolveMatrix.mKernelUnitLength;
      IntSize kernelSize = aConvolveMatrix.mKernelSize;
      IntPoint target = aConvolveMatrix.mTarget;
      nsIntMargin m(
          static_cast<int32_t>(ceil(kernelUnitLength.width * (target.x))),
          static_cast<int32_t>(ceil(kernelUnitLength.height * (target.y))),
          static_cast<int32_t>(
              ceil(kernelUnitLength.width * (kernelSize.width - target.x - 1))),
          static_cast<int32_t>(ceil(kernelUnitLength.height *
                                    (kernelSize.height - target.y - 1))));
      return mInputChangeRegions[0].Inflated(m);
    }

    nsIntRegion operator()(const OffsetAttributes& aOffset) {
      IntPoint offset = aOffset.mValue;
      return mInputChangeRegions[0].MovedBy(offset.x, offset.y);
    }

    nsIntRegion operator()(const DisplacementMapAttributes& aDisplacementMap) {
      int32_t scale = ceil(std::abs(aDisplacementMap.mScale));
      return mInputChangeRegions[0].Inflated(
          nsIntMargin(scale, scale, scale, scale));
    }

    nsIntRegion operator()(const TurbulenceAttributes& aTurbulence) {
      return nsIntRegion();
    }

    nsIntRegion operator()(const CompositeAttributes& aComposite) {
      return UnionOfRegions(mInputChangeRegions);
    }

    nsIntRegion operator()(const MergeAttributes& aMerge) {
      return UnionOfRegions(mInputChangeRegions);
    }

    nsIntRegion operator()(const GaussianBlurAttributes& aGaussianBlur) {
      const Size& stdDeviation = aGaussianBlur.mStdDeviation;
      int32_t dx = InflateSizeForBlurStdDev(stdDeviation.width);
      int32_t dy = InflateSizeForBlurStdDev(stdDeviation.height);
      return mInputChangeRegions[0].Inflated(nsIntMargin(dy, dx, dy, dx));
    }

    nsIntRegion operator()(const DropShadowAttributes& aDropShadow) {
      IntPoint offset = IntPoint::Truncate(aDropShadow.mOffset);
      nsIntRegion offsetRegion =
          mInputChangeRegions[0].MovedBy(offset.x, offset.y);
      Size stdDeviation = aDropShadow.mStdDeviation;
      int32_t dx = InflateSizeForBlurStdDev(stdDeviation.width);
      int32_t dy = InflateSizeForBlurStdDev(stdDeviation.height);
      nsIntRegion blurRegion =
          offsetRegion.Inflated(nsIntMargin(dy, dx, dy, dx));
      blurRegion.OrWith(mInputChangeRegions[0]);
      return blurRegion;
    }

    nsIntRegion operator()(const LightingAttributes& aLighting) {
      Size kernelUnitLength = aLighting.mKernelUnitLength;
      int32_t dx = ceil(kernelUnitLength.width);
      int32_t dy = ceil(kernelUnitLength.height);
      return mInputChangeRegions[0].Inflated(nsIntMargin(dy, dx, dy, dx));
    }

    nsIntRegion operator()(const ImageAttributes& aImage) {
      return nsIntRegion();
    }

    nsIntRegion operator()(const ToAlphaAttributes& aToAlpha) {
      return mInputChangeRegions[0];
    }
  };

  return aDescription.Attributes().match(
      PrimitiveAttributesMatcher(aDescription, aInputChangeRegions));
}

/* static */
nsIntRegion FilterSupport::ComputeResultChangeRegion(
    const FilterDescription& aFilter, const nsIntRegion& aSourceGraphicChange,
    const nsIntRegion& aFillPaintChange,
    const nsIntRegion& aStrokePaintChange) {
  const nsTArray<FilterPrimitiveDescription>& primitives = aFilter.mPrimitives;
  MOZ_RELEASE_ASSERT(!primitives.IsEmpty());

  nsTArray<nsIntRegion> resultChangeRegions;

  for (const auto& descr : primitives) {
    nsTArray<nsIntRegion> inputChangeRegions;
    for (size_t j = 0; j < descr.NumberOfInputs(); j++) {
      int32_t inputIndex = descr.InputPrimitiveIndex(j);
      nsIntRegion inputChangeRegion =
          ElementForIndex(inputIndex, resultChangeRegions, aSourceGraphicChange,
                          aFillPaintChange, aStrokePaintChange);
      inputChangeRegions.AppendElement(inputChangeRegion);
    }
    nsIntRegion changeRegion =
        ResultChangeRegionForPrimitive(descr, inputChangeRegions);
    changeRegion.AndWith(descr.PrimitiveSubregion());
    resultChangeRegions.AppendElement(changeRegion);
  }

  MOZ_RELEASE_ASSERT(!resultChangeRegions.IsEmpty());
  return resultChangeRegions.LastElement();
}

static float ResultOfZeroUnderTransferFunction(
    const ComponentTransferAttributes& aFunctionAttributes, int32_t channel) {
  switch (aFunctionAttributes.mTypes[channel]) {
    case SVGFEComponentTransferType::Table: {
      const nsTArray<float>& tableValues = aFunctionAttributes.mValues[channel];
      if (tableValues.Length() < 2) {
        return 0.0f;
      }
      return tableValues[0];
    }

    case SVGFEComponentTransferType::Discrete: {
      const nsTArray<float>& tableValues = aFunctionAttributes.mValues[channel];
      if (tableValues.Length() < 1) {
        return 0.0f;
      }
      return tableValues[0];
    }

    case SVGFEComponentTransferType::Linear: {
      const nsTArray<float>& values = aFunctionAttributes.mValues[channel];
      return values[kComponentTransferInterceptIndex];
    }

    case SVGFEComponentTransferType::Gamma: {
      const nsTArray<float>& values = aFunctionAttributes.mValues[channel];
      return values[kComponentTransferOffsetIndex];
    }

    case SVGFEComponentTransferType::Identity:
    default:
      return 0.0f;
  }
}

nsIntRegion FilterSupport::PostFilterExtentsForPrimitive(
    const FilterPrimitiveDescription& aDescription,
    const nsTArray<nsIntRegion>& aInputExtents) {
  struct PrimitiveAttributesMatcher {
    PrimitiveAttributesMatcher(const FilterPrimitiveDescription& aDescription,
                               const nsTArray<nsIntRegion>& aInputExtents)
        : mDescription(aDescription), mInputExtents(aInputExtents) {}

    const FilterPrimitiveDescription& mDescription;
    const nsTArray<nsIntRegion>& mInputExtents;

    nsIntRegion operator()(const EmptyAttributes& aEmptyAttributes) {
      return IntRect();
    }

    nsIntRegion operator()(const BlendAttributes& aBlend) {
      return ResultChangeRegionForPrimitive(mDescription, mInputExtents);
    }

    nsIntRegion operator()(const ColorMatrixAttributes& aColorMatrix) {
      if (aColorMatrix.mType == SVGFEColorMatrixType::Matrix) {
        const nsTArray<float>& values = aColorMatrix.mValues;
        if (values.Length() == 20 && values[19] > 0.0f) {
          return mDescription.PrimitiveSubregion();
        }
      }
      return mInputExtents[0];
    }

    nsIntRegion operator()(const MorphologyAttributes& aMorphology) {
      SVGMorphologyOperator op = aMorphology.mOperator;
      if (op == SVGMorphologyOperator::Erode) {
        return mInputExtents[0];
      }
      Size radii = aMorphology.mRadii;
      int32_t rx =
          std::clamp(int32_t(ceil(radii.width)), 0, kMorphologyMaxRadius);
      int32_t ry =
          std::clamp(int32_t(ceil(radii.height)), 0, kMorphologyMaxRadius);
      return mInputExtents[0].Inflated(nsIntMargin(ry, rx, ry, rx));
    }

    nsIntRegion operator()(const FloodAttributes& aFlood) {
      if (aFlood.mColor.a == 0.0f) {
        return IntRect();
      }
      return mDescription.PrimitiveSubregion();
    }

    nsIntRegion operator()(const TileAttributes& aTile) {
      return ResultChangeRegionForPrimitive(mDescription, mInputExtents);
    }

    nsIntRegion operator()(
        const ComponentTransferAttributes& aComponentTransfer) {
      if (ResultOfZeroUnderTransferFunction(aComponentTransfer, kChannelA) >
          0.0f) {
        return mDescription.PrimitiveSubregion();
      }
      return mInputExtents[0];
    }

    nsIntRegion operator()(const OpacityAttributes& aOpacity) {
      return ResultChangeRegionForPrimitive(mDescription, mInputExtents);
    }

    nsIntRegion operator()(const ConvolveMatrixAttributes& aConvolveMatrix) {
      if (!aConvolveMatrix.mPreserveAlpha && aConvolveMatrix.mBias > 0) {
        return mDescription.PrimitiveSubregion();
      }
      return ResultChangeRegionForPrimitive(mDescription, mInputExtents);
    }

    nsIntRegion operator()(const OffsetAttributes& aOffset) {
      return ResultChangeRegionForPrimitive(mDescription, mInputExtents);
    }

    nsIntRegion operator()(const DisplacementMapAttributes& aDisplacementMap) {
      return ResultChangeRegionForPrimitive(mDescription, mInputExtents);
    }

    nsIntRegion operator()(const TurbulenceAttributes& aTurbulence) {
      return mDescription.PrimitiveSubregion();
    }

    nsIntRegion operator()(const CompositeAttributes& aComposite) {
      SVGFECompositeOperator op = aComposite.mOperator;
      if (op == SVGFECompositeOperator::Arithmetic) {
        // The arithmetic composite primitive can draw outside the bounding
        // box of its source images.
        const nsTArray<float>& coefficients = aComposite.mCoefficients;
        MOZ_ASSERT(coefficients.Length() == 4);

        // The calculation is:
        // r = c[0] * in[0] * in[1] + c[1] * in[0] + c[2] * in[1] + c[3]
        nsIntRegion region;
        if (coefficients[0] > 0.0f) {
          region = mInputExtents[0].Intersect(mInputExtents[1]);
        }
        if (coefficients[1] > 0.0f) {
          region.OrWith(mInputExtents[0]);
        }
        if (coefficients[2] > 0.0f) {
          region.OrWith(mInputExtents[1]);
        }
        if (coefficients[3] > 0.0f) {
          region = mDescription.PrimitiveSubregion();
        }
        return region;
      }
      if (op == SVGFECompositeOperator::In) {
        return mInputExtents[0].Intersect(mInputExtents[1]);
      }
      return ResultChangeRegionForPrimitive(mDescription, mInputExtents);
    }

    nsIntRegion operator()(const MergeAttributes& aMerge) {
      return ResultChangeRegionForPrimitive(mDescription, mInputExtents);
    }

    nsIntRegion operator()(const GaussianBlurAttributes& aGaussianBlur) {
      return ResultChangeRegionForPrimitive(mDescription, mInputExtents);
    }

    nsIntRegion operator()(const DropShadowAttributes& aDropShadow) {
      return ResultChangeRegionForPrimitive(mDescription, mInputExtents);
    }

    nsIntRegion operator()(const LightingAttributes& aLighting) {
      return mDescription.PrimitiveSubregion();
    }

    nsIntRegion operator()(const ImageAttributes& aImage) {
      return mDescription.PrimitiveSubregion();
    }

    nsIntRegion operator()(const ToAlphaAttributes& aToAlpha) {
      return ResultChangeRegionForPrimitive(mDescription, mInputExtents);
    }
  };

  return aDescription.Attributes().match(
      PrimitiveAttributesMatcher(aDescription, aInputExtents));
}

/* static */
nsIntRegion FilterSupport::ComputePostFilterExtents(
    const FilterDescription& aFilter,
    const nsIntRegion& aSourceGraphicExtents) {
  const nsTArray<FilterPrimitiveDescription>& primitives = aFilter.mPrimitives;
  MOZ_RELEASE_ASSERT(!primitives.IsEmpty());
  nsTArray<nsIntRegion> postFilterExtents;

  for (const auto& descr : primitives) {
    nsIntRegion filterSpace = descr.FilterSpaceBounds();

    nsTArray<nsIntRegion> inputExtents;
    for (size_t j = 0; j < descr.NumberOfInputs(); j++) {
      int32_t inputIndex = descr.InputPrimitiveIndex(j);
      nsIntRegion inputExtent =
          ElementForIndex(inputIndex, postFilterExtents, aSourceGraphicExtents,
                          filterSpace, filterSpace);
      inputExtents.AppendElement(inputExtent);
    }
    nsIntRegion extent = PostFilterExtentsForPrimitive(descr, inputExtents);
    extent.AndWith(descr.PrimitiveSubregion());
    postFilterExtents.AppendElement(extent);
  }

  MOZ_RELEASE_ASSERT(!postFilterExtents.IsEmpty());
  return postFilterExtents.LastElement();
}

static nsIntRegion SourceNeededRegionForPrimitive(
    const FilterPrimitiveDescription& aDescription,
    const nsIntRegion& aResultNeededRegion, int32_t aInputIndex) {
  struct PrimitiveAttributesMatcher {
    PrimitiveAttributesMatcher(const FilterPrimitiveDescription& aDescription,
                               const nsIntRegion& aResultNeededRegion,
                               int32_t aInputIndex)
        : mDescription(aDescription),
          mResultNeededRegion(aResultNeededRegion),
          mInputIndex(aInputIndex) {}

    const FilterPrimitiveDescription& mDescription;
    const nsIntRegion& mResultNeededRegion;
    const int32_t mInputIndex;

    nsIntRegion operator()(const EmptyAttributes& aEmptyAttributes) {
      return nsIntRegion();
    }

    nsIntRegion operator()(const BlendAttributes& aBlend) {
      return mResultNeededRegion;
    }

    nsIntRegion operator()(const ColorMatrixAttributes& aColorMatrix) {
      return mResultNeededRegion;
    }

    nsIntRegion operator()(const MorphologyAttributes& aMorphology) {
      Size radii = aMorphology.mRadii;
      int32_t rx =
          std::clamp(int32_t(ceil(radii.width)), 0, kMorphologyMaxRadius);
      int32_t ry =
          std::clamp(int32_t(ceil(radii.height)), 0, kMorphologyMaxRadius);
      return mResultNeededRegion.Inflated(nsIntMargin(ry, rx, ry, rx));
    }

    nsIntRegion operator()(const FloodAttributes& aFlood) {
      MOZ_CRASH("GFX: this shouldn't be called for filters without inputs");
      return nsIntRegion();
    }

    nsIntRegion operator()(const TileAttributes& aTile) {
      return IntRect(INT32_MIN / 2, INT32_MIN / 2, INT32_MAX, INT32_MAX);
    }

    nsIntRegion operator()(
        const ComponentTransferAttributes& aComponentTransfer) {
      return mResultNeededRegion;
    }

    nsIntRegion operator()(const OpacityAttributes& aOpacity) {
      return mResultNeededRegion;
    }

    nsIntRegion operator()(const ConvolveMatrixAttributes& aConvolveMatrix) {
      Size kernelUnitLength = aConvolveMatrix.mKernelUnitLength;
      IntSize kernelSize = aConvolveMatrix.mKernelSize;
      IntPoint target = aConvolveMatrix.mTarget;
      nsIntMargin m(
          static_cast<int32_t>(
              ceil(kernelUnitLength.width * (kernelSize.width - target.x - 1))),
          static_cast<int32_t>(ceil(kernelUnitLength.height *
                                    (kernelSize.height - target.y - 1))),
          static_cast<int32_t>(ceil(kernelUnitLength.width * (target.x))),
          static_cast<int32_t>(ceil(kernelUnitLength.height * (target.y))));
      return mResultNeededRegion.Inflated(m);
    }

    nsIntRegion operator()(const OffsetAttributes& aOffset) {
      IntPoint offset = aOffset.mValue;
      return mResultNeededRegion.MovedBy(-nsIntPoint(offset.x, offset.y));
    }

    nsIntRegion operator()(const DisplacementMapAttributes& aDisplacementMap) {
      if (mInputIndex == 1) {
        return mResultNeededRegion;
      }
      int32_t scale = ceil(std::abs(aDisplacementMap.mScale));
      return mResultNeededRegion.Inflated(
          nsIntMargin(scale, scale, scale, scale));
    }

    nsIntRegion operator()(const TurbulenceAttributes& aTurbulence) {
      MOZ_CRASH("GFX: this shouldn't be called for filters without inputs");
      return nsIntRegion();
    }

    nsIntRegion operator()(const CompositeAttributes& aComposite) {
      return mResultNeededRegion;
    }

    nsIntRegion operator()(const MergeAttributes& aMerge) {
      return mResultNeededRegion;
    }

    nsIntRegion operator()(const GaussianBlurAttributes& aGaussianBlur) {
      const Size& stdDeviation = aGaussianBlur.mStdDeviation;
      int32_t dx = InflateSizeForBlurStdDev(stdDeviation.width);
      int32_t dy = InflateSizeForBlurStdDev(stdDeviation.height);
      return mResultNeededRegion.Inflated(nsIntMargin(dy, dx, dy, dx));
    }

    nsIntRegion operator()(const DropShadowAttributes& aDropShadow) {
      IntPoint offset = IntPoint::Truncate(aDropShadow.mOffset);
      nsIntRegion offsetRegion =
          mResultNeededRegion.MovedBy(-nsIntPoint(offset.x, offset.y));
      Size stdDeviation = aDropShadow.mStdDeviation;
      int32_t dx = InflateSizeForBlurStdDev(stdDeviation.width);
      int32_t dy = InflateSizeForBlurStdDev(stdDeviation.height);
      nsIntRegion blurRegion =
          offsetRegion.Inflated(nsIntMargin(dy, dx, dy, dx));
      blurRegion.OrWith(mResultNeededRegion);
      return blurRegion;
    }

    nsIntRegion operator()(const LightingAttributes& aLighting) {
      Size kernelUnitLength = aLighting.mKernelUnitLength;
      int32_t dx = ceil(kernelUnitLength.width);
      int32_t dy = ceil(kernelUnitLength.height);
      return mResultNeededRegion.Inflated(nsIntMargin(dy, dx, dy, dx));
    }

    nsIntRegion operator()(const ImageAttributes& aImage) {
      MOZ_CRASH("GFX: this shouldn't be called for filters without inputs");
      return nsIntRegion();
    }

    nsIntRegion operator()(const ToAlphaAttributes& aToAlpha) {
      return mResultNeededRegion;
    }
  };

  return aDescription.Attributes().match(PrimitiveAttributesMatcher(
      aDescription, aResultNeededRegion, aInputIndex));
}

/* static */
void FilterSupport::ComputeSourceNeededRegions(
    const FilterDescription& aFilter, const nsIntRegion& aResultNeededRegion,
    nsIntRegion& aSourceGraphicNeededRegion,
    nsIntRegion& aFillPaintNeededRegion,
    nsIntRegion& aStrokePaintNeededRegion) {
  const nsTArray<FilterPrimitiveDescription>& primitives = aFilter.mPrimitives;
  MOZ_ASSERT(!primitives.IsEmpty());
  if (primitives.IsEmpty()) {
    return;
  }

  nsTArray<nsIntRegion> primitiveNeededRegions;
  primitiveNeededRegions.AppendElements(primitives.Length());

  primitiveNeededRegions.LastElement() = aResultNeededRegion;

  for (int32_t i = primitives.Length() - 1; i >= 0; --i) {
    const FilterPrimitiveDescription& descr = primitives[i];
    nsIntRegion neededRegion = primitiveNeededRegions[i];
    neededRegion.AndWith(descr.PrimitiveSubregion());

    for (size_t j = 0; j < descr.NumberOfInputs(); j++) {
      int32_t inputIndex = descr.InputPrimitiveIndex(j);
      MOZ_ASSERT(inputIndex < i, "bad input index");
      nsIntRegion* inputNeededRegion =
          const_cast<nsIntRegion*>(&ElementForIndex(
              inputIndex, primitiveNeededRegions, aSourceGraphicNeededRegion,
              aFillPaintNeededRegion, aStrokePaintNeededRegion));
      inputNeededRegion->Or(*inputNeededRegion, SourceNeededRegionForPrimitive(
                                                    descr, neededRegion, j));
    }
  }

  // Clip original SourceGraphic to first filter region.
  const FilterPrimitiveDescription& firstDescr = primitives[0];
  aSourceGraphicNeededRegion.AndWith(firstDescr.FilterSpaceBounds());
}

// FilterPrimitiveDescription

FilterPrimitiveDescription::FilterPrimitiveDescription()
    : mAttributes(EmptyAttributes()),
      mOutputColorSpace(ColorSpace::SRGB),
      mIsTainted(false) {}

FilterPrimitiveDescription::FilterPrimitiveDescription(
    PrimitiveAttributes&& aAttributes)
    : mAttributes(std::move(aAttributes)),
      mOutputColorSpace(ColorSpace::SRGB),
      mIsTainted(false) {}

bool FilterPrimitiveDescription::operator==(
    const FilterPrimitiveDescription& aOther) const {
  return mFilterPrimitiveSubregion.IsEqualInterior(
             aOther.mFilterPrimitiveSubregion) &&
         mFilterSpaceBounds.IsEqualInterior(aOther.mFilterSpaceBounds) &&
         mOutputColorSpace == aOther.mOutputColorSpace &&
         mIsTainted == aOther.mIsTainted &&
         mInputPrimitives == aOther.mInputPrimitives &&
         mInputColorSpaces == aOther.mInputColorSpaces &&
         mAttributes == aOther.mAttributes;
}

// FilterDescription

bool FilterDescription::operator==(const FilterDescription& aOther) const {
  return mPrimitives == aOther.mPrimitives;
}

}  // namespace gfx
}  // namespace mozilla
