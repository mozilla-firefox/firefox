/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageOps.h"

#include "ClippedImage.h"
#include "Decoder.h"
#include "DecoderFactory.h"
#include "DynamicImage.h"
#include "FrozenImage.h"
#include "IDecodingTask.h"
#include "Image.h"
#include "ImageMetadata.h"
#include "OrientedImage.h"
#include "SourceBuffer.h"
#include "imgIContainer.h"
#include "mozilla/gfx/2D.h"
#include "nsNetUtil.h"  // for NS_NewBufferedInputStream
#include "nsStreamUtils.h"

using namespace mozilla::gfx;

namespace mozilla::image {

/* static */
already_AddRefed<Image> ImageOps::Freeze(Image* aImage) {
  RefPtr<Image> frozenImage = new FrozenImage(aImage);
  return frozenImage.forget();
}

/* static */
already_AddRefed<imgIContainer> ImageOps::Freeze(imgIContainer* aImage) {
  nsCOMPtr<imgIContainer> frozenImage =
      new FrozenImage(static_cast<Image*>(aImage));
  return frozenImage.forget();
}

/* static */
already_AddRefed<Image> ImageOps::Clip(Image* aImage, nsIntRect aClip,
                                       const Maybe<CSSSize>& aSVGViewportSize) {
  RefPtr<Image> clippedImage =
      new ClippedImage(aImage, aClip, aSVGViewportSize);
  return clippedImage.forget();
}

/* static */
already_AddRefed<imgIContainer> ImageOps::Clip(
    imgIContainer* aImage, nsIntRect aClip,
    const Maybe<CSSSize>& aSVGViewportSize) {
  nsCOMPtr<imgIContainer> clippedImage =
      new ClippedImage(static_cast<Image*>(aImage), aClip, aSVGViewportSize);
  return clippedImage.forget();
}

/* static */
already_AddRefed<Image> ImageOps::Orient(Image* aImage,
                                         Orientation aOrientation) {
  if (aOrientation.IsIdentity()) {
    return do_AddRef(aImage);
  }
  RefPtr<Image> image = new OrientedImage(aImage, aOrientation);
  return image.forget();
}

/* static */
already_AddRefed<imgIContainer> ImageOps::Orient(imgIContainer* aImage,
                                                 Orientation aOrientation) {
  return Orient(static_cast<Image*>(aImage), aOrientation);
}

/* static */
already_AddRefed<imgIContainer> ImageOps::Unorient(imgIContainer* aImage) {
  return Orient(aImage, aImage->GetOrientation().Reversed());
}

/* static */
already_AddRefed<imgIContainer> ImageOps::CreateFromDrawable(
    gfxDrawable* aDrawable) {
  nsCOMPtr<imgIContainer> drawableImage = new DynamicImage(aDrawable);
  return drawableImage.forget();
}

class ImageOps::ImageBufferImpl final : public ImageOps::ImageBuffer {
 public:
  explicit ImageBufferImpl(already_AddRefed<SourceBuffer> aSourceBuffer)
      : mSourceBuffer(aSourceBuffer) {}

 protected:
  ~ImageBufferImpl() override = default;

  already_AddRefed<SourceBuffer> GetSourceBuffer() const override {
    RefPtr<SourceBuffer> sourceBuffer = mSourceBuffer;
    return sourceBuffer.forget();
  }

 private:
  RefPtr<SourceBuffer> mSourceBuffer;
};

/* static */ already_AddRefed<ImageOps::ImageBuffer>
ImageOps::CreateImageBuffer(already_AddRefed<nsIInputStream> aInputStream) {
  nsCOMPtr<nsIInputStream> inputStream = std::move(aInputStream);
  MOZ_ASSERT(inputStream);

  nsresult rv;

  // Prepare the input stream.
  if (!NS_InputStreamIsBuffered(inputStream)) {
    nsCOMPtr<nsIInputStream> bufStream;
    rv = NS_NewBufferedInputStream(getter_AddRefs(bufStream),
                                   inputStream.forget(), 1024);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return nullptr;
    }
    inputStream = std::move(bufStream);
  }

  // Figure out how much data we've been passed.
  uint64_t length;
  rv = inputStream->Available(&length);
  if (NS_FAILED(rv) || length > UINT32_MAX) {
    return nullptr;
  }

  // Write the data into a SourceBuffer.
  RefPtr<SourceBuffer> sourceBuffer = new SourceBuffer();
  sourceBuffer->ExpectLength(length);
  rv = sourceBuffer->AppendFromInputStream(inputStream, length);
  if (NS_FAILED(rv)) {
    return nullptr;
  }
  // Make sure our sourceBuffer is marked as complete.
  if (sourceBuffer->IsComplete()) {
    NS_WARNING(
        "The SourceBuffer was unexpectedly marked as complete. This may "
        "indicate either an OOM condition, or that imagelib was not "
        "initialized properly.");
    return nullptr;
  }
  sourceBuffer->Complete(NS_OK);

  RefPtr<ImageBuffer> imageBuffer = new ImageBufferImpl(sourceBuffer.forget());
  return imageBuffer.forget();
}

/* static */
nsresult ImageOps::DecodeMetadata(already_AddRefed<nsIInputStream> aInputStream,
                                  const nsACString& aMimeType,
                                  ImageMetadata& aMetadata) {
  nsCOMPtr<nsIInputStream> inputStream = std::move(aInputStream);
  RefPtr<ImageBuffer> buffer = CreateImageBuffer(inputStream.forget());
  return DecodeMetadata(buffer, aMimeType, aMetadata);
}

/* static */
nsresult ImageOps::DecodeMetadata(ImageBuffer* aBuffer,
                                  const nsACString& aMimeType,
                                  ImageMetadata& aMetadata) {
  if (!aBuffer) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<SourceBuffer> sourceBuffer = aBuffer->GetSourceBuffer();
  if (NS_WARN_IF(!sourceBuffer)) {
    return NS_ERROR_FAILURE;
  }

  // Create a decoder.
  DecoderType decoderType =
      DecoderFactory::GetDecoderType(PromiseFlatCString(aMimeType).get());
  DecoderFlags decoderFlags = DefaultDecoderFlags();
  decoderFlags |= DecoderFlags::FIRST_FRAME_ONLY;
  RefPtr<Decoder> decoder = DecoderFactory::CreateAnonymousMetadataDecoder(
      decoderType, WrapNotNull(sourceBuffer), decoderFlags);
  if (!decoder) {
    return NS_ERROR_FAILURE;
  }

  // Run the decoder synchronously.
  RefPtr<IDecodingTask> task =
      new AnonymousDecodingTask(WrapNotNull(decoder), /* aResumable */ false);
  task->Run();
  if (!decoder->GetDecodeDone() || decoder->HasError()) {
    return NS_ERROR_FAILURE;
  }

  aMetadata = decoder->GetImageMetadata();
  if (aMetadata.GetNativeSizes().IsEmpty() && aMetadata.HasSize()) {
    aMetadata.AddNativeSize(aMetadata.GetSize());
  }

  return NS_OK;
}

/* static */ already_AddRefed<gfx::SourceSurface> ImageOps::DecodeToSurface(
    already_AddRefed<nsIInputStream> aInputStream, const nsACString& aMimeType,
    uint32_t aFlags, const Maybe<IntSize>& aSize /* = Nothing() */) {
  nsCOMPtr<nsIInputStream> inputStream = std::move(aInputStream);
  RefPtr<ImageBuffer> buffer = CreateImageBuffer(inputStream.forget());
  return DecodeToSurface(buffer, aMimeType, aFlags, aSize);
}

/* static */ already_AddRefed<gfx::SourceSurface> ImageOps::DecodeToSurface(
    ImageBuffer* aBuffer, const nsACString& aMimeType, uint32_t aFlags,
    const Maybe<IntSize>& aSize /* = Nothing() */) {
  if (!aBuffer) {
    return nullptr;
  }
  DecoderType decoderType =
      DecoderFactory::GetDecoderType(PromiseFlatCString(aMimeType).get());
  RefPtr<SourceBuffer> buffer = aBuffer->GetSourceBuffer();
  return DecodeToSurface(buffer, decoderType, aFlags, aSize);
}

already_AddRefed<gfx::SourceSurface> ImageOps::DecodeToSurface(
    SourceBuffer* aSourceBuffer, DecoderType aDecoderType, uint32_t aFlags,
    const Maybe<IntSize>& aSize /* = Nothing() */) {
  if (NS_WARN_IF(!aSourceBuffer)) {
    return nullptr;
  }
  RefPtr<Decoder> decoder = DecoderFactory::CreateAnonymousDecoder(
      aDecoderType, WrapNotNull(aSourceBuffer), aSize,
      DecoderFlags::FIRST_FRAME_ONLY, ToSurfaceFlags(aFlags));
  if (!decoder) {
    return nullptr;
  }
  // Run the decoder synchronously.
  RefPtr<IDecodingTask> task =
      new AnonymousDecodingTask(WrapNotNull(decoder), /* aResumable */ false);
  task->Run();
  if (!decoder->GetDecodeDone() || decoder->HasError()) {
    return nullptr;
  }
  // Pull out the surface.
  RawAccessFrameRef frame = decoder->GetCurrentFrameRef();
  if (!frame) {
    return nullptr;
  }
  return frame->GetSourceSurface();
}

}  // namespace mozilla::image
