/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_ImageOps_h
#define mozilla_image_ImageOps_h

#include "ImageMetadata.h"
#include "Units.h"
#include "nsCOMPtr.h"
#include "nsRect.h"

class gfxDrawable;
class imgIContainer;
class nsIInputStream;

namespace mozilla {

namespace gfx {
class SourceSurface;
}

namespace image {

class Image;
struct Orientation;
class SourceBuffer;
enum class DecoderType : uint8_t;

class ImageOps {
 public:
  class ImageBuffer {
   public:
    NS_INLINE_DECL_THREADSAFE_REFCOUNTING(ImageOps::ImageBuffer);

   protected:
    friend class ImageOps;

    ImageBuffer() = default;
    virtual ~ImageBuffer() = default;

    virtual already_AddRefed<SourceBuffer> GetSourceBuffer() const = 0;
  };

  /**
   * Creates a version of an existing image which does not animate and is frozen
   * at the first frame.
   *
   * @param aImage         The existing image.
   */
  static already_AddRefed<Image> Freeze(Image* aImage);
  static already_AddRefed<imgIContainer> Freeze(imgIContainer* aImage);

  /**
   * Creates a clipped version of an existing image. Animation is unaffected.
   *
   * @param aImage           The existing image.
   * @param aClip            The rectangle to clip the image against.
   * @param aSVGViewportSize The specific viewort size of aImage. Unless aImage
   *                         is a vector image without intrinsic size, this
   *                         argument should be passed as Nothing().
   */
  static already_AddRefed<Image> Clip(
      Image* aImage, nsIntRect aClip,
      const Maybe<CSSSize>& aSVGViewportSize = Nothing());
  static already_AddRefed<imgIContainer> Clip(
      imgIContainer* aImage, nsIntRect aClip,
      const Maybe<CSSSize>& aSVGViewportSize = Nothing());

  /**
   * Creates a version of an existing image which is rotated and/or flipped to
   * the specified orientation.
   *
   * @param aImage         The existing image.
   * @param aOrientation   The desired orientation.
   */
  static already_AddRefed<Image> Orient(Image* aImage,
                                        Orientation aOrientation);
  static already_AddRefed<imgIContainer> Orient(imgIContainer* aImage,
                                                Orientation aOrientation);

  /**
   * Creates a version of an existing image which undoes any rotation and/or
   * flipping that it has automatically handled.
   *
   * This only undoes the effect of a RasterImage's automatic orientation
   * handling.
   */
  static already_AddRefed<imgIContainer> Unorient(imgIContainer* aImage);

  /**
   * Creates an image from a gfxDrawable.
   *
   * @param aDrawable      The gfxDrawable.
   */
  static already_AddRefed<imgIContainer> CreateFromDrawable(
      gfxDrawable* aDrawable);

  /**
   * Create a buffer to be used with DecodeMetadata and DecodeToSurface. Reusing
   * an ImageBuffer representing the given input stream is more efficient if one
   * has multiple Decode* calls to make on that stream.
   *
   * @param aInputStream An input stream containing an encoded image. The
   * ownership is taken.
   * @return An image buffer derived from the input stream.
   */
  static already_AddRefed<ImageBuffer> CreateImageBuffer(
      already_AddRefed<nsIInputStream> aInputStream);

  /**
   * Decodes an image's metadata from an nsIInputStream into the given
   * structure. This function may be called off-main-thread.
   *
   * @param aInputStream An input stream containing an encoded image. Ownership
   * is taken.
   * @param aMimeType The MIME type of the image.
   * @param aMetadata Where the image metadata is stored upon success.
   * @return The status of the operation.
   */
  static nsresult DecodeMetadata(already_AddRefed<nsIInputStream> aInputStream,
                                 const nsACString& aMimeType,
                                 ImageMetadata& aMetadata);

  /**
   * Same as above but takes an ImageBuffer instead of nsIInputStream.
   */
  static nsresult DecodeMetadata(ImageBuffer* aBuffer,
                                 const nsACString& aMimeType,
                                 ImageMetadata& aMetadata);

  /**
   * Decodes an image from an nsIInputStream directly into a SourceSurface,
   * without ever creating an Image or imgIContainer (which are mostly
   * main-thread-only). That means that this function may be called
   * off-main-thread.
   *
   * @param aInputStream An input stream containing an encoded image. The
   * ownership is taken.
   * @param aMimeType The MIME type of the image.
   * @param aFlags Flags of the imgIContainer::FLAG_DECODE_* variety.
   * @return A SourceSurface containing the first frame of the image at its
   *         intrinsic size, or nullptr if the image cannot be decoded.
   */
  static already_AddRefed<gfx::SourceSurface> DecodeToSurface(
      already_AddRefed<nsIInputStream> aInputStream,
      const nsACString& aMimeType, uint32_t aFlags,
      const Maybe<gfx::IntSize>& aSize = Nothing());

  /**
   * Same as above but takes an ImageBuffer instead of nsIInputStream.
   */
  static already_AddRefed<gfx::SourceSurface> DecodeToSurface(
      ImageBuffer* aBuffer, const nsACString& aMimeType, uint32_t aFlags,
      const Maybe<gfx::IntSize>& aSize = Nothing());

  /* Same as above but with a SourceBuffer + DecoderType */
  static already_AddRefed<gfx::SourceSurface> DecodeToSurface(
      SourceBuffer* aSourceBuffer, image::DecoderType aDecoderType,
      uint32_t aFlags, const Maybe<gfx::IntSize>& aSize = Nothing());

 private:
  class ImageBufferImpl;

  // This is a static utility class, so disallow instantiation.
  virtual ~ImageOps() = 0;
};

}  // namespace image
}  // namespace mozilla

#endif  // mozilla_image_ImageOps_h
