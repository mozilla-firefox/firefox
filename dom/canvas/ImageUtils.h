/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ImageBitmapFormatUtils_h
#define mozilla_dom_ImageBitmapFormatUtils_h

#include <cstdint>

#include "nsTArrayForwardDeclare.h"

namespace mozilla {

template <typename T>
class Maybe;

namespace layers {
class Image;
}

class ErrorResult;

namespace dom {

struct ChannelPixelLayout;
enum class ImageBitmapFormat : uint8_t;

typedef nsTArray<ChannelPixelLayout> ImagePixelLayout;

/*
 * ImageUtils is a wrapper around layers::Image. It provides three unified
 * methods to all sub-classes of layers::Image, which are:
 *
 * (1) GetFormat() converts the image's format into ImageBitmapFormat enum.
 * (2) GetBufferLength() returns the number of bytes that are used to store
 *     the image's underlying raw data.
 *
 * In theory, the functionalities of this class could be merged into the
 * interface of layers::Image. However, this is designed as a isolated wrapper
 * because we don't want to pollute the layers::Image's interface with methods
 * that are only meaningful to the ImageBitmap.
 */
class ImageUtils {
 public:
  class Impl;
  ImageUtils() = delete;
  ImageUtils(const ImageUtils&) = delete;
  ImageUtils(ImageUtils&&) = delete;
  ImageUtils& operator=(const ImageUtils&) = delete;
  ImageUtils& operator=(ImageUtils&&) = delete;

  explicit ImageUtils(layers::Image* aImage);
  ~ImageUtils();

  Maybe<ImageBitmapFormat> GetFormat() const;

  uint32_t GetBufferLength() const;

 protected:
  Impl* mImpl;
};

}  // namespace dom
}  // namespace mozilla

#endif /* mozilla_dom_ImageBitmapFormatUtils_h */
