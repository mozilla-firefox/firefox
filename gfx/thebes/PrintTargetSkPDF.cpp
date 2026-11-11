/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PrintTargetSkPDF.h"

#include "ImageOps.h"
#include "imgIEncoder.h"
#include "include/codec/SkCodec.h"
#include "include/codec/SkEncodedImageFormat.h"
#include "include/codec/SkEncodedOrigin.h"
#include "include/core/SkStream.h"
#include "include/private/SkEncodedInfo.h"
#include "mozilla/StaticPrefs_print.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/image/ImageUtils.h"
#include "mozilla/image/SourceBuffer.h"
#include "nsISafeOutputStream.h"
#include "nsJPEGEncoder.h"
#include "nsString.h"
#include "skia/include/core/SkCanvas.h"
#include "skia/include/core/SkStream.h"
#include "skia/include/docs/SkPDFDocument.h"
#include "skia/src/pdf/SkPDFUtils.h"

#ifdef ACCESSIBILITY
#  include "mozilla/a11y/PdfStructTreeBuilder.h"
#endif

using namespace mozilla::image;
using namespace mozilla;

namespace {

static SkEncodedOrigin OrientationToSkEncodedOrigin(
    const Orientation& aOrientation) {
  const bool flipped = aOrientation.flip == Flip::Horizontal;
  switch (aOrientation.rotation) {
    case Angle::D0:
      return flipped ? kTopRight_SkEncodedOrigin : kTopLeft_SkEncodedOrigin;
    case Angle::D90:
      return flipped ? kLeftTop_SkEncodedOrigin : kRightTop_SkEncodedOrigin;
    case Angle::D180:
      return flipped ? kBottomLeft_SkEncodedOrigin
                     : kBottomRight_SkEncodedOrigin;
    case Angle::D270:
      return flipped ? kRightBottom_SkEncodedOrigin
                     : kLeftBottom_SkEncodedOrigin;
  }
  MOZ_ASSERT_UNREACHABLE("Unknown angle");
  return kTopLeft_SkEncodedOrigin;
}

// Minimal SkCodec subclass used to give SkPDF the properties of jpeg data, so
// that it can embed it directly. SkPDF never asks it for pixels.
class JpegSkCodec final : public SkCodec {
 public:
  static std::unique_ptr<SkCodec> Make(sk_sp<const SkData> aData) {
    RefPtr<SourceBuffer> buffer = MakeRefPtr<SourceBuffer>();
    buffer->AdoptData(
        const_cast<char*>(reinterpret_cast<const char*>(aData->bytes())),
        aData->size(),
        [](void*, size_t) -> void* {
          MOZ_DIAGNOSTIC_CRASH("Shouldn't need to reallocate data");
          return nullptr;
        },
        [](void*) { /* do nothing for free */ });
    buffer->Complete(NS_OK);

    ImageMetadata metadata;
    if (NS_FAILED(
            ImageOps::DecodeMetadata(buffer, DecoderType::JPEG, metadata)) ||
        !metadata.HasSize()) {
      return nullptr;
    }
    Maybe<EmbeddableJPEGColorSpace> colorSpace =
        metadata.GetEmbeddableJPEGColorSpace();
    if (!colorSpace) {
      return nullptr;
    }
    Orientation orientation = metadata.GetOrientation();
    UnorientedIntSize size = orientation.ToUnoriented(metadata.GetSize());
    auto color = *colorSpace == EmbeddableJPEGColorSpace::Grayscale
                     ? SkEncodedInfo::kGray_Color
                     : SkEncodedInfo::kYUV_Color;
    auto info = SkEncodedInfo::Make(size.width, size.height, color,
                                    SkEncodedInfo::kOpaque_Alpha, 8);
    return std::unique_ptr<JpegSkCodec>(
        new JpegSkCodec(std::move(info), SkMemoryStream::Make(std::move(aData)),
                        OrientationToSkEncodedOrigin(orientation)));
  }

 protected:
  SkEncodedImageFormat onGetEncodedFormat() const override {
    return SkEncodedImageFormat::kJPEG;
  }
  Result onGetPixels(const SkImageInfo&, void*, size_t, const Options&,
                     int*) override {
    MOZ_ASSERT_UNREACHABLE("SkPDF shouldn't decode jpeg data");
    return kUnimplemented;
  }

 private:
  JpegSkCodec(SkEncodedInfo&& aInfo, std::unique_ptr<SkStream> aStream,
              SkEncodedOrigin aOrigin)
      : SkCodec(std::move(aInfo), skcms_PixelFormat_RGB_888, std::move(aStream),
                aOrigin) {}
};

static std::unique_ptr<SkCodec> DecodeJpeg(sk_sp<const SkData> aData) {
  return JpegSkCodec::Make(std::move(aData));
}

static bool EncodeJpeg(SkWStream* aDst, const SkPixmap& aSrc, int aQuality) {
  uint32_t inputFormat;
  switch (aSrc.colorType()) {
    case kBGRA_8888_SkColorType:
      inputFormat = imgIEncoder::INPUT_FORMAT_HOSTARGB;
      break;
    case kRGBA_8888_SkColorType:
      inputFormat = imgIEncoder::INPUT_FORMAT_RGBA;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected jpeg encoding format");
      return false;
  }

  RefPtr<nsJPEGEncoder> encoder = new nsJPEGEncoder();
  nsAutoString options;
  options.AppendPrintf("quality=%d", aQuality);
  nsresult rv = encoder->InitFromData(
      static_cast<const uint8_t*>(aSrc.addr()), 0, aSrc.width(), aSrc.height(),
      aSrc.rowBytes(), inputFormat, options, ""_ns);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  char* buf = nullptr;
  uint32_t size = 0;
  encoder->GetImageBuffer(&buf);
  encoder->GetImageBufferUsed(&size);
  return buf && size && aDst->write(buf, size);
}

}  // namespace

namespace mozilla::gfx {

class GkSkWStream final : public SkWStream {
 public:
  // Used for the dummy mRefState stream.
  GkSkWStream() = default;

  explicit GkSkWStream(nsIOutputStream* aStream) : mStream(aStream) {
    MOZ_ASSERT(mStream);
  }
  ~GkSkWStream() override {
    // Ensure we close the stream when we're done if we haven't done it earlier.
    (void)NS_WARN_IF(NS_FAILED(closeStream(/* aCommit = */ false)));
  }

  bool write(const void* aBuf, size_t aSize) override {
    if (!mStream) {
      mWritten += aSize;
      return true;
    }
    const auto* data = reinterpret_cast<const char*>(aBuf);
    do {
      uint32_t wrote = 0;
      if (NS_WARN_IF(NS_FAILED(mStream->Write(data, aSize, &wrote)))) {
        return false;
      }
      mWritten += wrote;
      data += wrote;
      aSize -= wrote;
    } while (aSize);
    NS_ASSERTION(aSize == 0, "not everything was written to the file");
    return true;
  }
  void flush() override {
    if (!mStream) {
      return;
    }
    (void)NS_WARN_IF(NS_FAILED(mStream->Flush()));
  }
  size_t bytesWritten() const override { return mWritten; }

  // Close the stream so that its file handle is released. Safe streams discard
  // their output if aCommit = false, or commit it with Finish() instead.
  nsresult closeStream(bool aCommit) {
    nsCOMPtr stream = std::move(mStream);
    if (!stream) {
      return NS_OK;
    }
    if (aCommit) {
      if (nsCOMPtr<nsISafeOutputStream> safeStream =
              do_QueryInterface(stream)) {
        return safeStream->Finish();
      }
    }
    return stream->Close();
  }

 private:
  nsCOMPtr<nsIOutputStream> mStream;
  size_t mWritten = 0;
};

struct PrintTargetSkPDF::SkPDFDocState {
  explicit SkPDFDocState(UniquePtr<GkSkWStream> aStream)
      : mOStream(std::move(aStream)) {}

  // Do not hand out references to this object. It holds a non-owning reference
  // to mOStream, so must not outlive mOStream. Order here ensures that mDoc is
  // destroyed before mOStream, which it wraps.
  UniquePtr<GkSkWStream> mOStream;
  sk_sp<SkDocument> mDoc;
  SkCanvas* mCanvas = nullptr;  // Owned by mDoc.
};

PrintTargetSkPDF::PrintTargetSkPDF(UniquePtr<GkSkWStream> aStream)
    : mState(new SkPDFDocState(std::move(aStream))),
      mRefState(new SkPDFDocState(std::make_unique<GkSkWStream>())) {}

PrintTargetSkPDF::~PrintTargetSkPDF() {
  Finish();  // ensure stream is flushed
}

already_AddRefed<PrintTargetSkPDF> PrintTargetSkPDF::CreateOrNull(
    nsIOutputStream* aStream) {
  return do_AddRef(
      new PrintTargetSkPDF(std::make_unique<GkSkWStream>(aStream)));
}

static SkPDF::Metadata GetDefaultMetadata() {
  SkPDF::Metadata metadata;
  metadata.fEncodingQuality = StaticPrefs::print_skpdf_image_encoding_quality();
  // XXX Chromium sets fRasterDPI to 300, should we do the same? But we'd need
  // to deal with it on the caller too, since otherwise the page ends up tiny.
  metadata.jpegDecoder = DecodeJpeg;
  metadata.jpegEncoder = EncodeJpeg;
  return metadata;
}

nsresult PrintTargetSkPDF::BeginPrinting(const nsAString& aTitle,
                                         const nsAString& aPrintToFileName,
                                         uint64_t aInnerWindowId,
                                         int32_t aStartPage, int32_t aEndPage) {
  // We need to create the SkPDFDocument here rather than in CreateOrNull
  // because it's only now that we are given aTitle which we want for the
  // PDF metadata.

  NS_ConvertUTF16toUTF8 title(aTitle);
  SkPDF::Metadata metadata = GetDefaultMetadata();
  metadata.fTitle = SkString(title.get(), title.Length());
  metadata.fCreator = "Firefox";
  SkPDF::DateTime now = {0};
  SkPDFUtils::GetDateTime(&now);
  metadata.fCreation = now;
  metadata.fModified = now;

  metadata.jpegDecoder = DecodeJpeg;
  metadata.jpegEncoder = EncodeJpeg;

#ifdef ACCESSIBILITY
  // structRoot needs to survive until SkPDF::MakeDocument returns.
  SkPDF::StructureElementNode structRoot = {};
  if (auto* builder =
          mozilla::a11y::PdfStructTreeBuilder::Get(aInnerWindowId)) {
    if (builder->BuildStructTree(structRoot)) {
      metadata.fStructureElementTreeRoot = &structRoot;
      metadata.fOutline = SkPDF::Metadata::Outline::StructureElementHeaders;
    }
  }
#endif

  // SkDocument stores a non-owning raw pointer to aStream
  mState->mDoc = SkPDF::MakeDocument(mState->mOStream.get(), metadata);
  return mState->mDoc ? NS_OK : NS_ERROR_FAILURE;
}

nsresult PrintTargetSkPDF::BeginPage(const IntSize& aSizeInPoints) {
  mState->mCanvas =
      mState->mDoc->beginPage(aSizeInPoints.width, aSizeInPoints.height);
  if (!mState->mCanvas) {
    return NS_ERROR_FAILURE;
  }
  return PrintTarget::BeginPage(aSizeInPoints);
}

nsresult PrintTargetSkPDF::EndPage() {
  mState->mCanvas = nullptr;
  mPageDT = nullptr;
  return PrintTarget::EndPage();
}

nsresult PrintTargetSkPDF::AbortPrinting() {
  mState->mDoc->abort();
  return PrintTarget::AbortPrinting();
}

nsresult PrintTargetSkPDF::EndPrinting() {
  mState->mDoc->close();
  if (mRefState->mDoc) {
    mRefState->mDoc->close();
  }
  mState->mCanvas = nullptr;
  mPageDT = nullptr;
  return mState->mOStream->closeStream(/* aCommit = */ true);
}

already_AddRefed<DrawTarget> PrintTargetSkPDF::MakeDrawTarget(
    const IntSize& aSize, DrawEventRecorder* aRecorder) {
  if (!mState->mCanvas) {
    return nullptr;
  }
  mPageDT = Factory::CreateDrawTargetWithSkCanvas(
      mState->mCanvas, /* aSupportsGlyphSourceText */ true);
  if (!mPageDT) {
    mState->mCanvas = nullptr;
    return nullptr;
  }
  if (aRecorder) {
    return CreateRecordingDrawTarget(aRecorder, mPageDT);
  }
  return do_AddRef(mPageDT);
}

already_AddRefed<DrawTarget> PrintTargetSkPDF::GetReferenceDrawTarget() {
  if (!mRefDT) {
    // SkDocument stores a non-owning raw pointer to aStream
    mRefState->mDoc =
        SkPDF::MakeDocument(mRefState->mOStream.get(), GetDefaultMetadata());
    if (!mRefState->mDoc) {
      return nullptr;
    }
    mRefState->mCanvas = mRefState->mDoc->beginPage(1, 1);
    if (!mRefState->mCanvas) {
      return nullptr;
    }
    mRefDT = Factory::CreateDrawTargetWithSkCanvas(mRefState->mCanvas);
  }
  return do_AddRef(mRefDT);
}

}  // namespace mozilla::gfx
