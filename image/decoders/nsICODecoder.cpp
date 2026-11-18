/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* This is a Cross-Platform ICO Decoder, which should work everywhere, including
 * Big-Endian machines like the PowerPC. */

#include "nsICODecoder.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include "RasterImage.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/gfx/Swizzle.h"

using namespace mozilla::gfx;

namespace mozilla {
namespace image {

// Constants.
static const uint32_t ICOHEADERSIZE = 6;
static const uint32_t BITMAPINFOSIZE = bmp::InfoHeaderLength::WIN_ICO;

// ----------------------------------------
// Actual Data Processing
// ----------------------------------------

// Obtains the number of colors from the bits per pixel
uint16_t nsICODecoder::GetNumColors() {
  uint16_t numColors = 0;
  if (mBPP <= 8) {
    switch (mBPP) {
      case 1:
        numColors = 2;
        break;
      case 4:
        numColors = 16;
        break;
      case 8:
        numColors = 256;
        break;
      default:
        numColors = (uint16_t)-1;
    }
  }
  return numColors;
}

nsICODecoder::nsICODecoder(RasterImage* aImage)
    : Decoder(aImage),
      mLexer(Transition::To(ICOState::HEADER, ICOHEADERSIZE),
             Transition::TerminateSuccess()),
      mDirEntry(nullptr),
      mNumIcons(0),
      mCurrIcon(0),
      mBPP(0),
      mMaskRowSize(0),
      mCurrMaskLine(0),
      mIsCursor(false),
      mHasMaskAlpha(false) {}

nsresult nsICODecoder::FinishInternal() {
  // We shouldn't be called in error cases
  MOZ_ASSERT(!HasError(), "Shouldn't call FinishInternal after error!");

  return GetFinalStateFromContainedDecoder();
}

nsresult nsICODecoder::FinishWithErrorInternal() {
  // No need to assert !mInFrame here because this condition is enforced by
  // mContainedDecoder.
  return GetFinalStateFromContainedDecoder();
}

nsresult nsICODecoder::GetFinalStateFromContainedDecoder() {
  if (!mContainedDecoder) {
    return NS_OK;
  }

  // Let the contained decoder finish up if necessary.
  FlushContainedDecoder();

  // Make our state the same as the state of the contained decoder.
  mDecodeDone = mContainedDecoder->GetDecodeDone();
  mProgress |= mContainedDecoder->TakeProgress();
  mInvalidRect.UnionRect(mInvalidRect, mContainedDecoder->TakeInvalidRect());
  mCurrentFrame = mContainedDecoder->GetCurrentFrameRef();

  // Finalize the frame which we deferred to ensure we could modify the final
  // result (e.g. to apply the BMP mask).
  MOZ_ASSERT(!mContainedDecoder->GetFinalizeFrames());
  if (mCurrentFrame) {
    mCurrentFrame->FinalizeSurface();
  }

  // Propagate errors.
  nsresult rv =
      HasError() || mContainedDecoder->HasError() ? NS_ERROR_FAILURE : NS_OK;

  MOZ_ASSERT(NS_FAILED(rv) || !mCurrentFrame || mCurrentFrame->IsFinished());
  return rv;
}

LexerTransition<ICOState> nsICODecoder::ReadHeader(const char* aData) {
  // If the third byte is 1, this is an icon. If 2, a cursor.
  if ((aData[2] != 1) && (aData[2] != 2)) {
    return Transition::TerminateFailure();
  }
  mIsCursor = (aData[2] == 2);

  // The fifth and sixth bytes specify the number of resources in the file.
  mNumIcons = LittleEndian::readUint16(aData + 4);
  if (mNumIcons == 0) {
    return Transition::TerminateSuccess();  // Nothing to do.
  }

  // Downscale-during-decode can end up decoding different resources in the ICO
  // file depending on the target size. Since the resources are not necessarily
  // scaled versions of the same image, some may be transparent and some may not
  // be. We could be precise about transparency if we decoded the metadata of
  // every resource, but for now we don't and it's safest to assume that
  // transparency could be present.
  PostHasTransparency();

  return Transition::To(ICOState::DIR_ENTRY, ICODIRENTRYSIZE);
}

size_t nsICODecoder::FirstResourceOffset() const {
  MOZ_ASSERT(mNumIcons > 0,
             "Calling FirstResourceOffset before processing header");

  // The first resource starts right after the directory, which starts right
  // after the ICO header.
  return ICOHEADERSIZE + static_cast<size_t>(mNumIcons) * ICODIRENTRYSIZE;
}

LexerTransition<ICOState> nsICODecoder::ReadDirEntry(const char* aData) {
  mCurrIcon++;

  // Ensure the resource has an offset past the ICO headers.
  uint32_t offset = LittleEndian::readUint32(aData + 12);
  if (offset >= FirstResourceOffset()) {
    // Read the directory entry.
    IconDirEntryEx e;
    e.mWidth = aData[0];
    e.mHeight = aData[1];
    e.mColorCount = aData[2];
    e.mReserved = aData[3];
    e.mPlanes = LittleEndian::readUint16(aData + 4);
    e.mBitCount = LittleEndian::readUint16(aData + 6);
    e.mBytesInRes = LittleEndian::readUint32(aData + 8);
    e.mImageOffset = offset;
    e.mSize = OrientedIntSize(e.mWidth, e.mHeight);

    // Only accept entries with sufficient resource data to actually contain
    // some image data.
    if (e.mBytesInRes > BITMAPINFOSIZE) {
      mDirEntries.AppendElement(e);
    }
  }

  if (mCurrIcon == mNumIcons) {
    // If we detect anything that is likely not true in the dir entries sizes
    // then we don't trust any of them and just try to get the size of each
    // resource directly from the resource. This includes 0 sized entries, and
    // two entries having the same size.
    nsTArray<OrientedIntSize> sizes(mDirEntries.Length());
    for (const IconDirEntryEx& entry : mDirEntries) {
      sizes.AppendElement(entry.mSize);
    }
    std::sort(sizes.begin(), sizes.end(),
              [](const OrientedIntSize& aA, const OrientedIntSize& aB) {
                return std::tie(aA.width, aA.height) <
                       std::tie(aB.width, aB.height);
              });
    bool needsVerification =
        std::any_of(sizes.begin(), sizes.end(),
                    [](const OrientedIntSize& aSize) {
                      return aSize.width == 0 || aSize.height == 0;
                    }) ||
        std::adjacent_find(sizes.begin(), sizes.end()) != sizes.end();

    if (!needsVerification) {
      return Transition::To(ICOState::FINISHED_DIR_ENTRY, 0);
    }

    // Move all entries into the verification queue which will overwrite the
    // size in the dir entry with the size from the resource (or zeroes it on
    // failure). And then they get put back in mDirEntries once that is done.
    MOZ_ASSERT(mUnsizedDirEntries.IsEmpty());
    mUnsizedDirEntries.SwapElements(mDirEntries);
    return Transition::To(ICOState::ITERATE_UNSIZED_DIR_ENTRY, 0);
  }

  return Transition::To(ICOState::DIR_ENTRY, ICODIRENTRYSIZE);
}

LexerTransition<ICOState> nsICODecoder::IterateUnsizedDirEntry() {
  MOZ_ASSERT(!mUnsizedDirEntries.IsEmpty());

  if (!mDirEntry) {
    // The first time we are here, there is no entry selected. We must prepare a
    // new iterator for the contained decoder to advance as it wills. Cloning at
    // this point ensures it will begin at the end of the dir entries.
    mReturnIterator = mLexer.Clone(*mIterator, SIZE_MAX);
    if (mReturnIterator.isNothing()) {
      // If we cannot read further than this point, then there is no resource
      // data to read.
      return Transition::TerminateFailure();
    }
  } else {
    // We have already selected an entry which means a metadata decoder has
    // finished. Verify the size is valid and if so, add to the discovered
    // resources.
    if (mDirEntry->mSize.width > 0 && mDirEntry->mSize.height > 0) {
      mDirEntries.AppendElement(*mDirEntry);
    }

    // Remove the entry from the unsized list either way.
    mDirEntry = nullptr;
    mUnsizedDirEntries.RemoveElementAt(0);

    // Our iterator is at an unknown point, so reset it to the point that we
    // saved.
    mIterator = mLexer.Clone(*mReturnIterator, SIZE_MAX);
    if (mIterator.isNothing()) {
      MOZ_ASSERT_UNREACHABLE("Cannot re-clone return iterator");
      return Transition::TerminateFailure();
    }
  }

  // There are no more unsized entries, so we can finally decide which entry to
  // select for decoding.
  if (mUnsizedDirEntries.IsEmpty()) {
    mReturnIterator.reset();
    // Drop the probe contained decoder so its progress (e.g. FLAG_HAS_ERROR
    // from a failed probe) doesn't get merged into ours by a later
    // FlushContainedDecoder or GetFinalStateFromContainedDecoder. The real
    // decode path, if any, will create a fresh contained decoder.
    mContainedDecoder = nullptr;
    return Transition::To(ICOState::FINISHED_DIR_ENTRY, 0);
  }

  // Move to the resource data to start metadata decoding.
  mDirEntry = &mUnsizedDirEntries[0];
  // We ignored any dir entries whose offset didn't make sense before this.
  MOZ_ASSERT(static_cast<size_t>(mDirEntry->mImageOffset) >=
             FirstResourceOffset());
  size_t offsetToResource =
      static_cast<size_t>(mDirEntry->mImageOffset) - FirstResourceOffset();
  return Transition::ToUnbuffered(ICOState::FOUND_RESOURCE,
                                  ICOState::SKIP_TO_RESOURCE, offsetToResource);
}

LexerTransition<ICOState> nsICODecoder::FinishDirEntry() {
  MOZ_ASSERT(!mDirEntry);

  if (mDirEntries.IsEmpty()) {
    return Transition::TerminateFailure();
  }

  // If an explicit output size was specified, we'll try to select the resource
  // that matches it best below.
  const Maybe<OrientedIntSize> desiredSize = ExplicitOutputSize();

  // Determine the biggest resource. We always use the biggest resource for the
  // intrinsic size, and if we don't have a specific desired size, we select it
  // as the best resource as well.
  int32_t bestDelta = INT32_MIN;
  IconDirEntryEx* biggestEntry = nullptr;

  for (size_t i = 0; i < mDirEntries.Length(); ++i) {
    IconDirEntryEx& e = mDirEntries[i];
    mImageMetadata.AddNativeSize(e.mSize);

    if (!biggestEntry ||
        (e.mBitCount >= biggestEntry->mBitCount &&
         e.mSize.width * e.mSize.height >=
             biggestEntry->mSize.width * biggestEntry->mSize.height)) {
      biggestEntry = &e;

      if (!desiredSize) {
        mDirEntry = &e;
      }
    }

    if (desiredSize) {
      // Calculate the delta between this resource's size and the desired size,
      // so we can see if it is better than our current-best option.  In the
      // case of several equally-good resources, we use the last one. "Better"
      // in this case is determined by |delta|, a measure of the difference in
      // size between the entry we've found and the desired size. We will choose
      // the smallest resource that is greater than or equal to the desired size
      // (i.e. we assume it's better to downscale a larger icon than to upscale
      // a smaller one).
      int32_t delta = std::min(e.mSize.width - desiredSize->width,
                               e.mSize.height - desiredSize->height);
      if (!mDirEntry || (e.mBitCount >= mDirEntry->mBitCount &&
                         ((bestDelta < 0 && delta >= bestDelta) ||
                          (delta >= 0 && delta <= bestDelta)))) {
        mDirEntry = &e;
        bestDelta = delta;
      }
    }
  }

  MOZ_ASSERT(mDirEntry);
  MOZ_ASSERT(biggestEntry);

  // If this is a cursor, set the hotspot. We use the hotspot from the biggest
  // resource since we also use that resource for the intrinsic size.
  if (mIsCursor) {
    mImageMetadata.SetHotspot(biggestEntry->mXHotspot, biggestEntry->mYHotspot);
  }

  // We always report the biggest resource's size as the intrinsic size; this
  // is necessary for downscale-during-decode to work since we won't even
  // attempt to *upscale* while decoding.
  PostSize(biggestEntry->mSize.width, biggestEntry->mSize.height);
  if (WantsFrameCount()) {
    PostFrameCount(/* aFrameCount */ 1);
  }
  if (HasError()) {
    return Transition::TerminateFailure();
  }

  if (IsMetadataDecode()) {
    return Transition::TerminateSuccess();
  }

  if (mDirEntry->mSize == OutputSize()) {
    // If the resource we selected matches the output size perfectly, we don't
    // need to do any downscaling.
    MOZ_ASSERT_IF(desiredSize, mDirEntry->mSize == *desiredSize);
    MOZ_ASSERT_IF(!desiredSize, mDirEntry->mSize == Size());
  } else if (OutputSize().width < mDirEntry->mSize.width ||
             OutputSize().height < mDirEntry->mSize.height) {
    // Create a downscaler if we need to downscale.
    //
    // TODO(aosmond): This is the last user of Downscaler. We should switch this
    // to SurfacePipe as well so we can remove the code from tree.
    mDownscaler.emplace(OutputSize().ToUnknownSize());
  }

  // We ignored any dir entries whose offset didn't make sense before this.
  MOZ_ASSERT(static_cast<size_t>(mDirEntry->mImageOffset) >=
             FirstResourceOffset());
  size_t offsetToResource = mDirEntry->mImageOffset - FirstResourceOffset();
  return Transition::ToUnbuffered(ICOState::FOUND_RESOURCE,
                                  ICOState::SKIP_TO_RESOURCE, offsetToResource);
}

LexerTransition<ICOState> nsICODecoder::SniffResource(const char* aData) {
  MOZ_ASSERT(mDirEntry);

  // We have BITMAPINFOSIZE bytes buffered at this point. We know an embedded
  // BMP will have at least that many bytes by definition. We can also infer
  // that any valid embedded PNG will contain that many bytes as well because:
  //    BITMAPINFOSIZE
  //      <
  //    signature (8 bytes) +
  //    IHDR (12 bytes header + 13 bytes data)
  //    IDAT (12 bytes header)

  // We use the first PNGSIGNATURESIZE bytes to determine whether this resource
  // is a PNG or a BMP.
  bool isPNG =
      !memcmp(aData, nsPNGDecoder::pngSignatureBytes, PNGSIGNATURESIZE);
  if (isPNG) {
    if (mDirEntry->mBytesInRes <= BITMAPINFOSIZE) {
      if (!IsVerifyingResourceSizes()) {
        return Transition::TerminateFailure();
      }
      mDirEntry->mSize = OrientedIntSize(0, 0);
      return Transition::To(ICOState::ITERATE_UNSIZED_DIR_ENTRY, 0);
    }

    // Prepare a new iterator for the contained decoder to advance as it wills.
    // Cloning at the point ensures it will begin at the resource offset.
    Maybe<SourceBufferIterator> containedIterator =
        mLexer.Clone(*mIterator, mDirEntry->mBytesInRes);
    if (containedIterator.isNothing()) {
      if (!IsVerifyingResourceSizes()) {
        return Transition::TerminateFailure();
      }
      mDirEntry->mSize = OrientedIntSize(0, 0);
      return Transition::To(ICOState::ITERATE_UNSIZED_DIR_ENTRY, 0);
    }

    // Create a PNG decoder which will do the rest of the work for us.
    bool metadataDecode = mReturnIterator.isSome();
    Maybe<OrientedIntSize> expectedSize =
        metadataDecode ? Nothing() : Some(mDirEntry->mSize);
    mContainedDecoder = DecoderFactory::CreateDecoderForICOResource(
        DecoderType::PNG, std::move(containedIterator.ref()), WrapNotNull(this),
        metadataDecode, expectedSize);

    // Read in the rest of the PNG unbuffered.
    size_t toRead = mDirEntry->mBytesInRes - BITMAPINFOSIZE;
    return Transition::ToUnbuffered(ICOState::FINISHED_RESOURCE,
                                    ICOState::READ_RESOURCE, toRead);
  }

  // Make sure we have a sane size for the bitmap information header.
  int32_t bihSize = LittleEndian::readUint32(aData);
  if (bihSize != static_cast<int32_t>(BITMAPINFOSIZE)) {
    if (!IsVerifyingResourceSizes()) {
      return Transition::TerminateFailure();
    }
    mDirEntry->mSize = OrientedIntSize(0, 0);
    return Transition::To(ICOState::ITERATE_UNSIZED_DIR_ENTRY, 0);
  }

  // Read in the rest of the bitmap information header.
  return ReadBIH(aData);
}

LexerTransition<ICOState> nsICODecoder::ReadResource() {
  if (!FlushContainedDecoder()) {
    if (!IsVerifyingResourceSizes()) {
      return Transition::TerminateFailure();
    }
    // The lexer disallows transitioning out of an unbuffered read except to a
    // terminal state, so keep consuming bytes; FinishResource will see the
    // errored contained decoder and drop this resource then.
  }

  return Transition::ContinueUnbuffered(ICOState::READ_RESOURCE);
}

LexerTransition<ICOState> nsICODecoder::ReadBIH(const char* aData) {
  MOZ_ASSERT(mDirEntry);

  // Extract the BPP from the BIH header; it should be trusted over the one
  // we have from the ICO header which is usually set to 0.
  mBPP = LittleEndian::readUint16(aData + 14);

  // Check to make sure we have valid color settings.
  uint16_t numColors = GetNumColors();
  if (numColors == uint16_t(-1)) {
    if (!IsVerifyingResourceSizes()) {
      return Transition::TerminateFailure();
    }
    mDirEntry->mSize = OrientedIntSize(0, 0);
    return Transition::To(ICOState::ITERATE_UNSIZED_DIR_ENTRY, 0);
  }

  // The color table is present only if BPP is <= 8.
  MOZ_ASSERT_IF(mBPP > 8, numColors == 0);

  // The ICO format when containing a BMP does not include the 14 byte
  // bitmap file header. So we create the BMP decoder via the constructor that
  // tells it to skip this, and pass in the required data (dataOffset) that
  // would have been present in the header.
  uint32_t dataOffset =
      bmp::FILE_HEADER_LENGTH + BITMAPINFOSIZE + 4 * numColors;

  // Prepare a new iterator for the contained decoder to advance as it wills.
  // Cloning at the point ensures it will begin at the resource offset.
  Maybe<SourceBufferIterator> containedIterator =
      mLexer.Clone(*mIterator, mDirEntry->mBytesInRes);
  if (containedIterator.isNothing()) {
    if (!IsVerifyingResourceSizes()) {
      return Transition::TerminateFailure();
    }
    mDirEntry->mSize = OrientedIntSize(0, 0);
    return Transition::To(ICOState::ITERATE_UNSIZED_DIR_ENTRY, 0);
  }

  // Create a BMP decoder which will do most of the work for us; the exception
  // is the AND mask, which isn't present in standalone BMPs.
  bool metadataDecode = mReturnIterator.isSome();
  Maybe<OrientedIntSize> expectedSize =
      metadataDecode ? Nothing() : Some(mDirEntry->mSize);
  mContainedDecoder = DecoderFactory::CreateDecoderForICOResource(
      DecoderType::BMP, std::move(containedIterator.ref()), WrapNotNull(this),
      metadataDecode, expectedSize, Some(dataOffset));

  RefPtr<nsBMPDecoder> bmpDecoder =
      static_cast<nsBMPDecoder*>(mContainedDecoder.get());

  // Ensure the decoder has parsed at least the BMP's bitmap info header.
  if (!FlushContainedDecoder()) {
    if (!IsVerifyingResourceSizes()) {
      return Transition::TerminateFailure();
    }
    mDirEntry->mSize = OrientedIntSize(0, 0);
    return Transition::To(ICOState::ITERATE_UNSIZED_DIR_ENTRY, 0);
  }

  // If this is a metadata decode, FinishResource will any necessary checks.
  if (mContainedDecoder->IsMetadataDecode()) {
    return Transition::To(ICOState::FINISHED_RESOURCE, 0);
  }

  // Do we have an AND mask on this BMP? If so, we need to read it after we read
  // the BMP data itself.
  auto bmpDataLength =
      CheckedInt<uint32_t>(bmpDecoder->GetCompressedImageSize()) +
      4 * numColors;
  auto fullBmpLength = bmpDataLength + BITMAPINFOSIZE;
  if (!bmpDataLength.isValid() || !fullBmpLength.isValid() ||
      fullBmpLength.value() > mDirEntry->mBytesInRes) {
    // Claimed data length inside the bmp resource exceeds dir entry size.
    return Transition::TerminateFailure();
  }
  bool hasANDMask = fullBmpLength.value() < mDirEntry->mBytesInRes;
  ICOState afterBMPState =
      hasANDMask ? ICOState::PREPARE_FOR_MASK : ICOState::FINISHED_RESOURCE;

  // Read in the rest of the BMP unbuffered.
  return Transition::ToUnbuffered(afterBMPState, ICOState::READ_RESOURCE,
                                  bmpDataLength.value());
}

LexerTransition<ICOState> nsICODecoder::PrepareForMask() {
  MOZ_ASSERT(mDirEntry);

  // We have received all of the data required by the BMP decoder so flushing
  // here guarantees the decode has finished, if we have a valid file.
  if (!FlushContainedDecoder()) {
    return Transition::TerminateFailure();
  }

  if (!mContainedDecoder->GetDecodeDone()) {
    return Transition::TerminateFailure();
  }

  RefPtr<nsBMPDecoder> bmpDecoder =
      static_cast<nsBMPDecoder*>(mContainedDecoder.get());

  if (!bmpDecoder->GetImageData() || bmpDecoder->GetImageDataLength() == 0) {
    return Transition::TerminateFailure();
  }
  if (mDownscaler) {
    if (mDownscaler->TargetSize().width < 0 ||
        mDownscaler->TargetSize().height < 0 ||
        bmpDecoder->GetImageDataLength() !=
            static_cast<size_t>(mDownscaler->TargetSize().width *
                                mDownscaler->TargetSize().height * 4)) {
      return Transition::TerminateFailure();
    }
  } else {
    if (mDirEntry->mSize.width < 0 || mDirEntry->mSize.height < 0 ||
        bmpDecoder->GetImageDataLength() !=
            static_cast<size_t>(mDirEntry->mSize.width *
                                mDirEntry->mSize.height * 4)) {
      return Transition::TerminateFailure();
    }
  }

  uint16_t numColors = GetNumColors();
  MOZ_ASSERT(numColors != uint16_t(-1));

  // Determine the length of the AND mask.
  uint32_t bmpLengthWithHeader =
      BITMAPINFOSIZE + bmpDecoder->GetCompressedImageSize() + 4 * numColors;
  // We can't get here unless this is true.
  MOZ_ASSERT(bmpLengthWithHeader < mDirEntry->mBytesInRes);
  uint32_t maskLength = mDirEntry->mBytesInRes - bmpLengthWithHeader;

  // If the BMP provides its own transparency, we ignore the AND mask.
  if (bmpDecoder->HasTransparency()) {
    return Transition::ToUnbuffered(ICOState::FINISHED_RESOURCE,
                                    ICOState::SKIP_MASK, maskLength);
  }

  // Compute the row size for the mask.
  mMaskRowSize = ((mDirEntry->mSize.width + 31) / 32) * 4;  // + 31 to round up

  // If the expected size of the AND mask is larger than its actual size, then
  // we must have a truncated (and therefore corrupt) AND mask.
  uint32_t expectedLength = mMaskRowSize * mDirEntry->mSize.height;
  if (maskLength < expectedLength) {
    return Transition::TerminateFailure();
  }

  // If we're downscaling, the mask is the wrong size for the surface we've
  // produced, so we need to downscale the mask into a temporary buffer and then
  // combine the mask's alpha values with the color values from the image.
  if (mDownscaler) {
    mMaskBuffer =
        MakeUniqueFallible<uint8_t[]>(bmpDecoder->GetImageDataLength());
    if (NS_WARN_IF(!mMaskBuffer)) {
      return Transition::TerminateFailure();
    }
    nsresult rv = mDownscaler->BeginFrame(
        mDirEntry->mSize.ToUnknownSize(), Nothing(), mMaskBuffer.get(),
        /* aFormat = */ gfx::SurfaceFormat::B8G8R8A8,
        /* aFlipVertically = */ true);
    if (NS_FAILED(rv)) {
      return Transition::TerminateFailure();
    }
  }

  mCurrMaskLine = mDirEntry->mSize.height;
  return Transition::To(ICOState::READ_MASK_ROW, mMaskRowSize);
}

LexerTransition<ICOState> nsICODecoder::ReadMaskRow(const char* aData) {
  MOZ_ASSERT(mDirEntry);

  mCurrMaskLine--;

  uint8_t sawTransparency = 0;

  // Get the mask row we're reading.
  const uint8_t* mask = reinterpret_cast<const uint8_t*>(aData);
  const uint8_t* maskRowEnd = mask + mMaskRowSize;

  // Get the corresponding row of the mask buffer (if we're downscaling) or the
  // decoded image data (if we're not).
  uint32_t* decoded = nullptr;
  if (mDownscaler) {
    // Initialize the row to all white and fully opaque.
    memset(mDownscaler->RowBuffer(), 0xFF,
           mDirEntry->mSize.width * sizeof(uint32_t));

    decoded = reinterpret_cast<uint32_t*>(mDownscaler->RowBuffer());
  } else {
    RefPtr<nsBMPDecoder> bmpDecoder =
        static_cast<nsBMPDecoder*>(mContainedDecoder.get());
    uint32_t* imageData = bmpDecoder->GetImageData();
    if (!imageData) {
      return Transition::TerminateFailure();
    }

    decoded = imageData + mCurrMaskLine * mDirEntry->mSize.width;
  }

  MOZ_ASSERT(decoded);
  uint32_t* decodedRowEnd = decoded + mDirEntry->mSize.width;

  // Iterate simultaneously through the AND mask and the image data.
  while (mask < maskRowEnd) {
    uint8_t idx = *mask++;
    sawTransparency |= idx;
    for (uint8_t bit = 0x80; bit && decoded < decodedRowEnd; bit >>= 1) {
      // Clear pixel completely for transparency.
      if (idx & bit) {
        *decoded = 0;
      }
      decoded++;
    }
  }

  if (mDownscaler) {
    mDownscaler->CommitRow();
  }

  // If any bits are set in sawTransparency, then we know at least one pixel was
  // transparent.
  if (sawTransparency) {
    mHasMaskAlpha = true;
  }

  if (mCurrMaskLine == 0) {
    return Transition::To(ICOState::FINISH_MASK, 0);
  }

  return Transition::To(ICOState::READ_MASK_ROW, mMaskRowSize);
}

LexerTransition<ICOState> nsICODecoder::FinishMask() {
  // If we're downscaling, we now have the appropriate alpha values in
  // mMaskBuffer. We just need to transfer them to the image.
  if (mDownscaler) {
    // Retrieve the image data.
    RefPtr<nsBMPDecoder> bmpDecoder =
        static_cast<nsBMPDecoder*>(mContainedDecoder.get());
    uint8_t* imageData = reinterpret_cast<uint8_t*>(bmpDecoder->GetImageData());
    if (!imageData) {
      return Transition::TerminateFailure();
    }

    // Iterate through the alpha values, copying from mask to image.
    MOZ_ASSERT(mMaskBuffer);
    MOZ_ASSERT(bmpDecoder->GetImageDataLength() > 0);
    for (size_t i = 3; i < bmpDecoder->GetImageDataLength(); i += 4) {
      imageData[i] = mMaskBuffer[i];
    }
    int32_t stride = mDownscaler->TargetSize().width * sizeof(uint32_t);
    DebugOnly<bool> ret =
        // We know the format is OS_RGBA because we always assume bmp's inside
        // ico's are transparent.
        PremultiplyData(imageData, stride, SurfaceFormat::OS_RGBA, imageData,
                        stride, SurfaceFormat::OS_RGBA,
                        mDownscaler->TargetSize());
    MOZ_ASSERT(ret);
  }

  return Transition::To(ICOState::FINISHED_RESOURCE, 0);
}

LexerTransition<ICOState> nsICODecoder::FinishResource() {
  MOZ_ASSERT(mDirEntry);

  // We have received all of the data required by the PNG/BMP decoder so
  // flushing here guarantees the decode has finished.
  if (!FlushContainedDecoder()) {
    if (!IsVerifyingResourceSizes()) {
      return Transition::TerminateFailure();
    }
    mDirEntry->mSize = OrientedIntSize(0, 0);
    return Transition::To(ICOState::ITERATE_UNSIZED_DIR_ENTRY, 0);
  }

  if (!mContainedDecoder->GetDecodeDone()) {
    // We've sent as much data as the dir entry says for this resource, if it's
    // not done by now then something is corrupt.
    if (!IsVerifyingResourceSizes()) {
      return Transition::TerminateFailure();
    }
    mDirEntry->mSize = OrientedIntSize(0, 0);
    return Transition::To(ICOState::ITERATE_UNSIZED_DIR_ENTRY, 0);
  }

  // If it is a metadata decode, we're verifying every dir entry's size
  // against its resource (or filling in size info for entries that left it
  // unspecified). We believe the resource over the dir entry so we overwrite
  // the dir entry's size with the resource's reported size whether or not the
  // dir entry claimed one.
  if (mContainedDecoder->IsMetadataDecode()) {
    if (mContainedDecoder->HasSize()) {
      mDirEntry->mSize = mContainedDecoder->Size();
    } else {
      mDirEntry->mSize = OrientedIntSize(0, 0);
    }
    return Transition::To(ICOState::ITERATE_UNSIZED_DIR_ENTRY, 0);
  }

  // Raymond Chen says that 32bpp only are valid PNG ICOs
  // http://blogs.msdn.com/b/oldnewthing/archive/2010/10/22/10079192.aspx
  if (!mContainedDecoder->IsValidICOResource()) {
    return Transition::TerminateFailure();
  }

  // This size from the resource should match that from the dir entry.
  MOZ_ASSERT_IF(mContainedDecoder->HasSize(),
                mContainedDecoder->Size() == mDirEntry->mSize);

  return Transition::TerminateSuccess();
}

LexerResult nsICODecoder::DoDecode(SourceBufferIterator& aIterator,
                                   IResumable* aOnResume) {
  MOZ_ASSERT(!HasError(), "Shouldn't call DoDecode after error!");

  return mLexer.Lex(
      aIterator, aOnResume,
      [this](ICOState aState, const char* aData, size_t /*aLength*/) {
        switch (aState) {
          case ICOState::HEADER:
            return ReadHeader(aData);
          case ICOState::DIR_ENTRY:
            return ReadDirEntry(aData);
          case ICOState::FINISHED_DIR_ENTRY:
            return FinishDirEntry();
          case ICOState::ITERATE_UNSIZED_DIR_ENTRY:
            return IterateUnsizedDirEntry();
          case ICOState::SKIP_TO_RESOURCE:
            return Transition::ContinueUnbuffered(ICOState::SKIP_TO_RESOURCE);
          case ICOState::FOUND_RESOURCE:
            return Transition::To(ICOState::SNIFF_RESOURCE, BITMAPINFOSIZE);
          case ICOState::SNIFF_RESOURCE:
            return SniffResource(aData);
          case ICOState::READ_RESOURCE:
            return ReadResource();
          case ICOState::PREPARE_FOR_MASK:
            return PrepareForMask();
          case ICOState::READ_MASK_ROW:
            return ReadMaskRow(aData);
          case ICOState::FINISH_MASK:
            return FinishMask();
          case ICOState::SKIP_MASK:
            return Transition::ContinueUnbuffered(ICOState::SKIP_MASK);
          case ICOState::FINISHED_RESOURCE:
            return FinishResource();
          default:
            MOZ_CRASH("Unknown ICOState");
        }
      });
}

bool nsICODecoder::FlushContainedDecoder() {
  MOZ_ASSERT(mContainedDecoder);

  bool succeeded = true;

  // If we run out of data, the ICO decoder will get resumed when there's more
  // data available, as usual, so we don't need the contained decoder to get
  // resumed too. To avoid that, we provide an IResumable which just does
  // nothing. All the caller needs to do is flush when there is new data.
  LexerResult result = mContainedDecoder->Decode();
  if (result == LexerResult(TerminalState::FAILURE)) {
    succeeded = false;
  }

  MOZ_ASSERT(result != LexerResult(Yield::OUTPUT_AVAILABLE),
             "Unexpected yield");

  if (mContainedDecoder->HasError()) {
    succeeded = false;
  }

  // The contained decoder used during the resource-size verification phase is
  // a throwaway probe. Drop its progress and invalid rect instead of merging
  // them into ours, so a failed probe doesn't end up surfacing FLAG_HAS_ERROR
  // (or any other state) on the ICO decoder.
  if (IsVerifyingResourceSizes()) {
    mContainedDecoder->TakeProgress();
    mContainedDecoder->TakeInvalidRect();
  } else {
    // Make our state the same as the state of the contained decoder, and
    // propagate errors.
    mProgress |= mContainedDecoder->TakeProgress();
    mInvalidRect.UnionRect(mInvalidRect, mContainedDecoder->TakeInvalidRect());
  }

  return succeeded;
}

}  // namespace image
}  // namespace mozilla
