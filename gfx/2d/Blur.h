/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_BLUR_H_
#define MOZILLA_GFX_BLUR_H_

#include "mozilla/gfx/Point.h"
#include "mozilla/gfx/Rect.h"
#include "mozilla/gfx/Types.h"

class SkImage;
struct SkPoint;
class SkSurface;

namespace mozilla {
namespace gfx {

class DrawTargetSkia;

#ifdef _MSC_VER
#  pragma warning(disable : 4251)
#endif

/**
 * Implementation of a Gaussian blur.
 *
 * A Gaussian blur is good for blurring because, when done independently
 * in the horizontal and vertical directions, it matches the result that
 * would be obtained using a different (rotated) set of axes.
 *
 * This is a "service" class; the constructors set up all the information
 * based on the values.
 * The callers are responsible for creating and managing the backing surface
 * and passing the pointer to the data to the Blur() method.  This class does
 * not retain the pointer to the data outside of the Blur() call.
 *
 * A spread N makes each output pixel the maximum value of all source
 * pixels within a square of side length 2N+1 centered on the output pixel.
 */
class GFX2D_API GaussianBlur final {
 public:
  /** Constructs a Gaussian blur and computes the backing surface size.
   *
   * @param aRect The coordinates of the surface to create in device units.
   *
   * @param aSigma The blur sigma.
   *
   * @param aDirtyRect A pointer to a dirty rect, measured in device units, if
   *   available.  This will be used for optimizing the blur operation. It is
   *   safe to pass nullptr here.
   *
   * @param aSkipRect A pointer to a rect, measured in device units, that
   *   represents an area where blurring is unnecessary and shouldn't be done
   * for speed reasons. It is safe to pass nullptr here.
   *
   * @param aFormat The format of the surface's data.
   *
   * @param aClamp Whether clamping at border pixels is required.
   */
  GaussianBlur(const Rect& aRect, const IntSize& aSpreadRadius,
               const Point& aSigma, const Rect* aDirtyRect,
               const Rect* aSkipRect, SurfaceFormat aFormat = SurfaceFormat::A8,
               bool aClamp = false);

  explicit GaussianBlur(const Point& aSigma, bool aClamp = false);

  GaussianBlur() = default;

  void Init(const Rect& aRect, const IntSize& aSpreadRadius,
            const Point& aBlurSigma, const Rect* aDirtyRect,
            const Rect* aSkipRect, SurfaceFormat aFormat = SurfaceFormat::A8,
            bool aClamp = false);

  ~GaussianBlur() = default;

  /**
   * Return the size, in pixels, of the surface we'd use.
   */
  IntSize GetSize() const;

  /**
   * Return the format of the blur data.
   */
  SurfaceFormat GetFormat() const;

  /**
   * Return the stride, in bytes, of the surface we'd use.
   */
  int32_t GetStride() const;

  /**
   * Returns the device-space rectangle the surface covers.
   */
  IntRect GetRect() const;

  /**
   * Return a pointer to a dirty rect, as passed in to the constructor, or
   * nullptr if none was passed in.
   */
  Rect* GetDirtyRect();

  /**
   * Return the spread radius, in pixels.
   */
  IntSize GetSpreadRadius() const { return mSpreadRadius; }

  /**
   * Return the blur sigma.
   */
  Point GetBlurSigma() const { return mBlurSigma; }

  /**
   * Return the blur radius, in pixels.
   */
  IntSize GetBlurRadius() const { return mBlurRadius; }

  /**
   * Return the skip rect.
   */
  IntRect GetSkipRect() const { return mSkipRect; }

  /**
   * Return the minimum buffer size that should be given to Blur() method.  If
   * zero, the class is not properly setup for blurring.  Note that this
   * includes the extra three bytes on top of the stride*width, where something
   * like gfxImageSurface::GetDataSize() would report without it, even if it
   * happens to have the extra bytes.
   */
  size_t GetSurfaceAllocationSize() const;

  /**
   * Perform the blur in-place on the surface backed by specified surface data.
   * The size must be at least that returned by GetSurfaceAllocationSize() or
   * bad things will happen.
   */
  void Blur(uint8_t* aData, int32_t aStride, const IntSize& aSize,
            SurfaceFormat aFormat = SurfaceFormat::UNKNOWN) const;

  /**
   * Calculates a blur radius that, when used with box blur, approximates a
   * Gaussian blur with the given standard deviation.  The result of this
   * function should be used as the aBlurRadius parameter to GaussianBlur's
   * constructor, above.
   */
  static IntSize CalculateBlurRadius(const Point& aStandardDeviation);
  static Float CalculateBlurSigma(int32_t aBlurRadius);

  /**
   * Software blur filters clamp their standard deviation to this value, in
   * filter space, for performance reasons.
   */
  static constexpr Float kMaxSoftwareStdDeviation = 100.0f;

 private:
  /**
   * A rect indicating the area where blurring is unnecessary, and the blur
   * algorithm should skip over it.
   *
   * This is guaranteed to be 4-pixel aligned in the x axis.
   */
  IntRect mSkipRect;

  /**
   * The device-space rectangle the backing surface covers.
   */
  IntRect mRect;

  /**
   * A copy of the dirty rect passed to the constructor. This will only be valid
   * if mHasDirtyRect is true.
   */
  Rect mDirtyRect;

  /**
   * The spread radius, in pixels.
   */
  IntSize mSpreadRadius;

  /**
   * The blur sigma (standard deviation).
   */
  Point mBlurSigma;

  /**
   * The blur radius, in pixels.
   */
  IntSize mBlurRadius;

  /**
   * The format of the data passed to Blur()
   */
  SurfaceFormat mFormat = SurfaceFormat::UNKNOWN;

  /**
   * Whether clamping at border pixels is required.
   */
  bool mClamp = false;

  /**
   * The stride of the data passed to Blur()
   */
  int32_t mStride = 0;

  /**
   * The minimum size of the buffer needed for the Blur() operation.
   */
  size_t mSurfaceAllocationSize = 0;

  /**
   * Whether mDirtyRect contains valid data.
   */
  bool mHasDirtyRect = false;

  bool Spread(uint8_t* aData, int32_t aStride, const IntSize& aSize,
              SurfaceFormat aFormat) const;

  friend class DrawTargetSkia;

  static SkImage* BlurAlphaMask(SkImage* aAlphaImage, const Point& aSigma,
                                SkPoint& aOffset);
  bool BlurSkSurface(SkSurface* aSurface) const;
};

}  // namespace gfx
}  // namespace mozilla

#endif /* MOZILLA_GFX_BLUR_H_ */
