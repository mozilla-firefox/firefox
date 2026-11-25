/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This is a cross-platform BMP Decoder, which should work everywhere,
// including big-endian machines like the PowerPC.
//
// BMP is a format that has been extended multiple times. To understand the
// decoder you need to understand this history. The summary of the history
// below was determined from the following documents.
//
// - http://www.fileformat.info/format/bmp/egff.htm
// - http://www.fileformat.info/format/os2bmp/egff.htm
// - http://fileformats.archiveteam.org/wiki/BMP
// - http://fileformats.archiveteam.org/wiki/OS/2_BMP
// - https://en.wikipedia.org/wiki/BMP_file_format
// - https://upload.wikimedia.org/wikipedia/commons/c/c4/BMPfileFormat.png
//
// WINDOWS VERSIONS OF THE BMP FORMAT
// ----------------------------------
// WinBMPv1.
// - This version is no longer used and can be ignored.
//
// WinBMPv2.
// - First is a 14 byte file header that includes: the magic number ("BM"),
//   file size, and offset to the pixel data (|mDataOffset|).
// - Next is a 12 byte info header which includes: the info header size
//   (mBIHSize), width, height, number of color planes, and bits-per-pixel
//   (|mBpp|) which must be 1, 4, 8 or 24.
// - Next is the semi-optional color table, which has length 2^|mBpp| and has 3
//   bytes per value (BGR). The color table is required if |mBpp| is 1, 4, or 8.
// - Next is an optional gap.
// - Next is the pixel data, which is pointed to by |mDataOffset|.
//
// WinBMPv3. This is the most widely used version.
// - It changed the info header to 40 bytes by taking the WinBMPv2 info
//   header, enlargening its width and height fields, and adding more fields
//   including: a compression type (|mCompression|) and number of colors
//   (|mNumColors|).
// - The semi-optional color table is now 4 bytes per value (BGR0), and its
//   length is |mNumColors|, or 2^|mBpp| if |mNumColors| is zero.
// - |mCompression| can be RGB (i.e. no compression), RLE4 (if |mBpp|==4) or
//   RLE8 (if |mBpp|==8) values.
//
// WinBMPv3-NT. A variant of WinBMPv3.
// - It did not change the info header layout from WinBMPv3.
// - |mBpp| can now be 16 or 32, in which case |mCompression| can be RGB or the
//   new BITFIELDS value; in the latter case an additional 12 bytes of color
//   bitfields follow the info header.
//
// BITMAPV2INFOHEADER. An Adobe extension to WinBMPv3.
// - It extended the info header to 52 bytes by appending the 12 bytes of RGB
//   color bitfields inside the info header itself, rather than following it
//   separately as in WinBMPv3-NT.
// - No alpha mask is present.
//
// BITMAPV3INFOHEADER. An Adobe extension to BITMAPV2INFOHEADER.
// - It extended the info header to 56 bytes by appending an additional 4-byte
//   alpha mask after the RGB bitfields.
//
// WinBMPv4.
// - It extended the info header to 108 bytes, including the 12 bytes of color
//   mask data from WinBMPv3-NT, plus alpha mask data, and also color-space and
//   gamma correction fields.
//
// WinBMPv5.
// - It extended the info header to 124 bytes, adding color profile data.
// - It also added an optional color profile table after the pixel data (and
//   another optional gap).
//
// WinBMPv3-ICO. This is a variant of WinBMPv3.
// - It's the BMP format used for BMP images within ICO files.
// - The only difference with WinBMPv3 is that if an image is 32bpp and has no
//   compression, then instead of treating the pixel data as 0RGB it is treated
//   as ARGB, but only if one or more of the A values are non-zero.
//
// Clipboard variants.
// - It's the BMP format used for BMP images captured from the clipboard.
// - It is missing the file header, containing the BM signature and the data
//   offset. Instead the data begins after the header.
// - If it uses BITFIELDS compression, then there is always an additional 12
//   bytes of data after the header that must be read. In WinBMPv4+, the masks
//   are supposed to be included in the header size, which are the values we use
//   for decoding purposes, but there is additional three masks following the
//   header which must be skipped to get to the pixel data.
//
// OS/2 VERSIONS OF THE BMP FORMAT
// -------------------------------
// OS2-BMPv1.
// - Almost identical to WinBMPv2; the differences are basically ignorable.
//
// OS2-BMPv2.
// - Similar to WinBMPv3.
// - The info header is 64 bytes but can be reduced to as little as 16; any
//   omitted fields are treated as zero. The first 40 bytes of these fields are
//   nearly identical to the WinBMPv3 info header; the remaining 24 bytes are
//   different.
// - Also adds compression types "Huffman 1D" and "RLE24", which we don't
//   support.
// - We treat OS2-BMPv2 files as if they are WinBMPv3 (i.e. ignore the extra 24
//   bytes in the info header), which in practice is good enough.

#include "nsBMPDecoder.h"

#include <stdlib.h>

#include <algorithm>

#include "ImageLogging.h"
#include "RasterImage.h"
#include "SurfacePipeFactory.h"
#include "gfxPlatform.h"
#include "mozilla/Attributes.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/UniquePtrExtensions.h"

using namespace mozilla::gfx;

namespace mozilla {
namespace image {
namespace bmp {

struct Compression {
  enum { RGB = 0, RLE8 = 1, RLE4 = 2, BITFIELDS = 3 };
};

// RLE escape codes and constants.
struct RLE {
  enum {
    ESCAPE = 0,
    ESCAPE_EOL = 0,
    ESCAPE_EOF = 1,
    ESCAPE_DELTA = 2,

    SEGMENT_LENGTH = 2,
    DELTA_LENGTH = 2
  };
};

}  // namespace bmp

using namespace bmp;

static double FixedPoint2Dot30_To_Double(uint32_t aFixed) {
  constexpr double factor = 1.0 / 1073741824.0;  // 2^-30
  return double(aFixed) * factor;
}

static float FixedPoint16Dot16_To_Float(uint32_t aFixed) {
  constexpr double factor = 1.0 / 65536.0;  // 2^-16
  return double(aFixed) * factor;
}

static float CalRbgEndpointToQcms(const CalRgbEndpoint& aIn,
                                  qcms_CIE_xyY& aOut) {
  aOut.x = FixedPoint2Dot30_To_Double(aIn.mX);
  aOut.y = FixedPoint2Dot30_To_Double(aIn.mY);
  aOut.Y = FixedPoint2Dot30_To_Double(aIn.mZ);
  return FixedPoint16Dot16_To_Float(aIn.mGamma);
}

static void ReadCalRgbEndpoint(const char* aData, uint32_t aEndpointOffset,
                               uint32_t aGammaOffset, CalRgbEndpoint& aOut) {
  aOut.mX = LittleEndian::readUint32(aData + aEndpointOffset);
  aOut.mY = LittleEndian::readUint32(aData + aEndpointOffset + 4);
  aOut.mZ = LittleEndian::readUint32(aData + aEndpointOffset + 8);
  aOut.mGamma = LittleEndian::readUint32(aData + aGammaOffset);
}

/// Sets the pixel data in aDecoded to the given values.
/// @param aDecoded pointer to pixel to be set, will be incremented to point to
/// the next pixel.
static void SetPixel(uint32_t*& aDecoded, uint8_t aRed, uint8_t aGreen,
                     uint8_t aBlue, uint8_t aAlpha = 0xFF) {
  *aDecoded++ = gfxPackedPixelNoPreMultiply(aAlpha, aRed, aGreen, aBlue);
}

static void SetPixel(uint32_t*& aDecoded, uint8_t idx,
                     const UniquePtr<ColorTableEntry[]>& aColors) {
  SetPixel(aDecoded, aColors[idx].mRed, aColors[idx].mGreen,
           aColors[idx].mBlue);
}

/// Sets two (or one if aCount = 1) pixels
/// @param aDecoded where the data is stored. Will be moved 4 resp 8 bytes
/// depending on whether one or two pixels are written.
/// @param aData The values for the two pixels
/// @param aCount Current count. Is decremented by one or two.
static void Set4BitPixel(uint32_t*& aDecoded, uint8_t aData, uint32_t& aCount,
                         const UniquePtr<ColorTableEntry[]>& aColors) {
  uint8_t idx = aData >> 4;
  SetPixel(aDecoded, idx, aColors);
  if (--aCount > 0) {
    idx = aData & 0xF;
    SetPixel(aDecoded, idx, aColors);
    --aCount;
  }
}

static mozilla::LazyLogModule sBMPLog("BMPDecoder");

// The length of the mBIHSize field in the info header.
static const uint32_t BIHSIZE_FIELD_LENGTH = 4;

nsBMPDecoder::nsBMPDecoder(RasterImage* aImage, State aState, size_t aLength,
                           bool aForClipboard)
    : Decoder(aImage),
      mLexer(Transition::To(aState, aLength), Transition::TerminateSuccess()),
      mIsWithinICO(false),
      mIsForClipboard(aForClipboard),
      mMayHaveTransparency(false),
      mDoesHaveTransparency(false),
      mNumColors(0),
      mColors(nullptr),
      mBytesPerColor(0),
      mPreGapLength(0),
      mPixelRowSize(0),
      mCurrentRow(0),
      mCurrentPos(0),
      mAbsoluteModeNumPixels(0) {}

// Constructor for normal BMP files or from the clipboard.
nsBMPDecoder::nsBMPDecoder(RasterImage* aImage, bool aForClipboard)
    : nsBMPDecoder(aImage,
                   aForClipboard ? State::INFO_HEADER_SIZE : State::FILE_HEADER,
                   aForClipboard ? BIHSIZE_FIELD_LENGTH : FILE_HEADER_LENGTH,
                   aForClipboard) {}

// Constructor used for WinBMPv3-ICO files, which lack a file header.
nsBMPDecoder::nsBMPDecoder(RasterImage* aImage, uint32_t aDataOffset)
    : nsBMPDecoder(aImage, State::INFO_HEADER_SIZE, BIHSIZE_FIELD_LENGTH,
                   /* aForClipboard */ false) {
  SetIsWithinICO();

  // Even though the file header isn't present in this case, the dataOffset
  // field is set as if it is, and so we must increment mPreGapLength
  // accordingly.
  mPreGapLength += FILE_HEADER_LENGTH;

  // This is the one piece of data we normally get from a BMP file header, so
  // it must be provided via an argument.
  mH.mDataOffset = aDataOffset;
}

nsBMPDecoder::~nsBMPDecoder() = default;

// Obtains the size of the compressed image resource.
uint32_t nsBMPDecoder::GetCompressedImageSize() const {
  // Keep this in sync with the overflow check in ReadInfoHeaderRest.
  // In the RGB case mImageSize might not be set, so compute it manually.
  MOZ_ASSERT(mPixelRowSize != 0);
  return mH.mCompression == Compression::RGB ? mPixelRowSize * AbsoluteHeight()
                                             : mH.mImageSize;
}

nsresult nsBMPDecoder::BeforeFinishInternal() {
  if (!IsMetadataDecode() && !mImageData) {
    return NS_ERROR_FAILURE;  // No image; something went wrong.
  }

  return NS_OK;
}

nsresult nsBMPDecoder::FinishInternal() {
  // We shouldn't be called in error cases.
  MOZ_ASSERT(!HasError(), "Can't call FinishInternal on error!");

  // We should never make multiple frames.
  MOZ_ASSERT(GetFrameCount() <= 1, "Multiple BMP frames?");

  // Send notifications if appropriate.
  if (!IsMetadataDecode() && HasSize()) {
    // We should have image data.
    MOZ_ASSERT(mImageData);

    // If it was truncated, fill in the missing pixels as black.
    while (mCurrentRow > 0) {
      uint32_t* dst = RowBuffer();
      while (mCurrentPos < mH.mWidth) {
        SetPixel(dst, 0, 0, 0);
        mCurrentPos++;
      }
      mCurrentPos = 0;
      FinishRow();
    }

    MOZ_ASSERT_IF(mDoesHaveTransparency, mMayHaveTransparency);

    // We have transparency if we either detected some in the image itself
    // (i.e., |mDoesHaveTransparency| is true) or we're in an ICO, which could
    // mean we have an AND mask that provides transparency (i.e., |mIsWithinICO|
    // is true).
    // XXX(seth): We can tell when we create the decoder if the AND mask is
    // present, so we could be more precise about this.
    const Opacity opacity = mDoesHaveTransparency || mIsWithinICO
                                ? Opacity::SOME_TRANSPARENCY
                                : Opacity::FULLY_OPAQUE;

    PostFrameStop(opacity);
    PostDecodeDone();
  }

  return NS_OK;
}

// ----------------------------------------
// Actual Data Processing
// ----------------------------------------

void BitFields::Value::Set(uint32_t aMask) {
  mMask = aMask;

  // Handle this exceptional case first. The chosen values don't matter
  // (because a mask of zero will always give a value of zero) except that
  // mBitWidth:
  // - shouldn't be zero, because that would cause an infinite loop in Get();
  // - shouldn't be 5 or 8, because that could cause a false positive match in
  //   IsR5G5B5() or IsR8G8B8().
  if (mMask == 0x0) {
    mRightShift = 0;
    mBitWidth = 1;
    return;
  }

  // Find the rightmost 1.
  uint8_t i;
  for (i = 0; i < 32; i++) {
    if (mMask & (1 << i)) {
      break;
    }
  }
  mRightShift = i;

  // Now find the leftmost 1 in the same run of 1s. (If there are multiple runs
  // of 1s -- which isn't valid -- we'll behave as if only the lowest run was
  // present, which seems reasonable.)
  for (i = i + 1; i < 32; i++) {
    if (!(mMask & (1 << i))) {
      break;
    }
  }
  mBitWidth = i - mRightShift;
}

MOZ_ALWAYS_INLINE uint8_t BitFields::Value::Get(uint32_t aValue) const {
  // Extract the unscaled value.
  uint32_t v = (aValue & mMask) >> mRightShift;

  // Idea: to upscale v precisely we need to duplicate its bits, possibly
  // repeatedly, possibly partially in the last case, from bit 7 down to bit 0
  // in v2. For example:
  //
  // - mBitWidth=1:  v2 = v<<7 | v<<6 | ... | v<<1 | v>>0     k -> kkkkkkkk
  // - mBitWidth=2:  v2 = v<<6 | v<<4 | v<<2 | v>>0          jk -> jkjkjkjk
  // - mBitWidth=3:  v2 = v<<5 | v<<2 | v>>1                ijk -> ijkijkij
  // - mBitWidth=4:  v2 = v<<4 | v>>0                      hijk -> hijkhijk
  // - mBitWidth=5:  v2 = v<<3 | v>>2                     ghijk -> ghijkghi
  // - mBitWidth=6:  v2 = v<<2 | v>>4                    fghijk -> fghijkfg
  // - mBitWidth=7:  v2 = v<<1 | v>>6                   efghijk -> efghijke
  // - mBitWidth=8:  v2 = v>>0                         defghijk -> defghijk
  // - mBitWidth=9:  v2 = v>>1                        cdefghijk -> cdefghij
  // - mBitWidth=10: v2 = v>>2                       bcdefghijk -> bcdefghi
  // - mBitWidth=11: v2 = v>>3                      abcdefghijk -> abcdefgh
  // - etc.
  //
  uint8_t v2 = 0;
  int32_t i;  // must be a signed integer
  for (i = 8 - mBitWidth; i > 0; i -= mBitWidth) {
    v2 |= v << uint32_t(i);
  }
  v2 |= v >> uint32_t(-i);
  return v2;
}

MOZ_ALWAYS_INLINE uint8_t BitFields::Value::GetAlpha(uint32_t aValue,
                                                     bool& aHasAlphaOut) const {
  if (mMask == 0x0) {
    return 0xff;
  }
  aHasAlphaOut = true;
  return Get(aValue);
}

MOZ_ALWAYS_INLINE uint8_t BitFields::Value::Get5(uint32_t aValue) const {
  MOZ_ASSERT(mBitWidth == 5);
  uint32_t v = (aValue & mMask) >> mRightShift;
  return (v << 3u) | (v >> 2u);
}

MOZ_ALWAYS_INLINE uint8_t BitFields::Value::Get8(uint32_t aValue) const {
  MOZ_ASSERT(mBitWidth == 8);
  uint32_t v = (aValue & mMask) >> mRightShift;
  return v;
}

void BitFields::SetR5G5B5() {
  mRed.Set(0x7c00);
  mGreen.Set(0x03e0);
  mBlue.Set(0x001f);
}

void BitFields::SetR8G8B8() {
  mRed.Set(0xff0000);
  mGreen.Set(0xff00);
  mBlue.Set(0x00ff);
}

bool BitFields::IsR5G5B5() const {
  return mRed.mBitWidth == 5 && mGreen.mBitWidth == 5 && mBlue.mBitWidth == 5 &&
         mAlpha.mMask == 0x0;
}

bool BitFields::IsR8G8B8() const {
  return mRed.mBitWidth == 8 && mGreen.mBitWidth == 8 && mBlue.mBitWidth == 8 &&
         mAlpha.mMask == 0x0;
}

uint32_t* nsBMPDecoder::RowBuffer() { return mRowBuffer.get() + mCurrentPos; }

void nsBMPDecoder::ClearRowBufferRemainder() {
  int32_t len = mH.mWidth - mCurrentPos;
  memset(RowBuffer(), mMayHaveTransparency ? 0 : 0xFF, len * sizeof(uint32_t));
}

void nsBMPDecoder::FinishRow() {
  mPipe.WriteBuffer(mRowBuffer.get());
  Maybe<SurfaceInvalidRect> invalidRect = mPipe.TakeInvalidRect();
  if (invalidRect) {
    PostInvalidation(invalidRect->mInputSpaceRect,
                     Some(invalidRect->mOutputSpaceRect));
  }
  mCurrentRow--;
}

LexerResult nsBMPDecoder::DoDecode(SourceBufferIterator& aIterator,
                                   IResumable* aOnResume) {
  MOZ_ASSERT(!HasError(), "Shouldn't call DoDecode after error!");

  return mLexer.Lex(
      aIterator, aOnResume,
      [this](State aState, const char* aData, size_t aLength) {
        switch (aState) {
          case State::FILE_HEADER:
            return ReadFileHeader(aData, aLength);
          case State::INFO_HEADER_SIZE:
            return ReadInfoHeaderSize(aData, aLength);
          case State::INFO_HEADER_REST:
            return ReadInfoHeaderRest(aData, aLength);
          case State::BITFIELDS:
            return ReadBitfields(aData, aLength);
          case State::SKIP_TO_COLOR_PROFILE:
            return Transition::ContinueUnbuffered(State::SKIP_TO_COLOR_PROFILE);
          case State::FOUND_COLOR_PROFILE:
            return Transition::To(State::COLOR_PROFILE,
                                  mH.mColorSpace.mProfile.mLength);
          case State::COLOR_PROFILE:
            return ReadColorProfile(aData, aLength);
          case State::ALLOCATE_SURFACE:
            return AllocateSurface();
          case State::COLOR_TABLE:
            return ReadColorTable(aData, aLength);
          case State::GAP:
            return SkipGap();
          case State::AFTER_GAP:
            return AfterGap();
          case State::PIXEL_ROW:
            return ReadPixelRow(aData);
          case State::RLE_SEGMENT:
            return ReadRLESegment(aData);
          case State::RLE_DELTA:
            return ReadRLEDelta(aData);
          case State::RLE_ABSOLUTE:
            return ReadRLEAbsolute(aData, aLength);
          default:
            MOZ_CRASH("Unknown State");
        }
      });
}

LexerTransition<nsBMPDecoder::State> nsBMPDecoder::ReadFileHeader(
    const char* aData, size_t aLength) {
  mPreGapLength += aLength;

  bool signatureOk = aData[0] == 'B' && aData[1] == 'M';
  if (!signatureOk) {
    return Transition::TerminateFailure();
  }

  // We ignore the filesize (aData + 2) and reserved (aData + 6) fields.

  mH.mDataOffset = LittleEndian::readUint32(aData + 10);

  return Transition::To(State::INFO_HEADER_SIZE, BIHSIZE_FIELD_LENGTH);
}

// We read the info header in two steps: (a) read the mBIHSize field to
// determine how long the header is; (b) read the rest of the header.
LexerTransition<nsBMPDecoder::State> nsBMPDecoder::ReadInfoHeaderSize(
    const char* aData, size_t aLength) {
  mH.mBIHSize = LittleEndian::readUint32(aData);

  // Data offset can be wrong so fix it using the BIH size.
  if (!mIsForClipboard && mH.mDataOffset < mPreGapLength + mH.mBIHSize) {
    mH.mDataOffset = mPreGapLength + mH.mBIHSize;
  }

  mPreGapLength += aLength;

  bool bihSizeOk = mH.mBIHSize == InfoHeaderLength::WIN_V2 ||
                   mH.mBIHSize == InfoHeaderLength::WIN_V3 ||
                   mH.mBIHSize == InfoHeaderLength::BITMAPV2INFOHEADER ||
                   mH.mBIHSize == InfoHeaderLength::BITMAPV3INFOHEADER ||
                   mH.mBIHSize == InfoHeaderLength::WIN_V4 ||
                   mH.mBIHSize == InfoHeaderLength::WIN_V5 ||
                   (mH.mBIHSize >= InfoHeaderLength::OS2_V2_MIN &&
                    mH.mBIHSize <= InfoHeaderLength::OS2_V2_MAX);
  if (!bihSizeOk) {
    return Transition::TerminateFailure();
  }
  // ICO BMPs must have a WinBMPv3 header. nsICODecoder should have already
  // terminated decoding if this isn't the case.
  MOZ_ASSERT_IF(mIsWithinICO, mH.mBIHSize == InfoHeaderLength::WIN_V3);

  return Transition::To(State::INFO_HEADER_REST,
                        mH.mBIHSize - BIHSIZE_FIELD_LENGTH);
}

LexerTransition<nsBMPDecoder::State> nsBMPDecoder::ReadInfoHeaderRest(
    const char* aData, size_t aLength) {
  mPreGapLength += aLength;

  // |mWidth| and |mHeight| may be signed (Windows) or unsigned (OS/2). We just
  // read as unsigned because in practice that's good enough.
  if (mH.mBIHSize == InfoHeaderLength::WIN_V2) {
    mH.mWidth = LittleEndian::readUint16(aData + 0);
    mH.mHeight = LittleEndian::readUint16(aData + 2);
    // We ignore the planes (aData + 4) field; it should always be 1.
    mH.mBpp = LittleEndian::readUint16(aData + 6);
  } else {
    mH.mWidth = LittleEndian::readUint32(aData + 0);
    mH.mHeight = LittleEndian::readUint32(aData + 4);
    // We ignore the planes (aData + 4) field; it should always be 1.
    mH.mBpp = LittleEndian::readUint16(aData + 10);

    // For OS2-BMPv2 the info header may be as little as 16 bytes, so be
    // careful for these fields.
    mH.mCompression = aLength >= 16 ? LittleEndian::readUint32(aData + 12) : 0;
    mH.mImageSize = aLength >= 20 ? LittleEndian::readUint32(aData + 16) : 0;
    // We ignore the xppm (aData + 20) and yppm (aData + 24) fields.
    mH.mNumColors = aLength >= 32 ? LittleEndian::readUint32(aData + 28) : 0;
    // We ignore the important_colors (aData + 36) field.

    // Read color management properties we may need later.
    if (aLength >= 56) {
      mH.mCsType = Some(
          static_cast<InfoColorSpace>(LittleEndian::readUint32(aData + 52)));
    }
    mH.mCsIntent = aLength >= 108 ? static_cast<InfoColorIntent>(
                                        LittleEndian::readUint32(aData + 104))
                                  : InfoColorIntent::IMAGES;

    if (mH.mCsType.isSome()) {
      switch (*mH.mCsType) {
        case InfoColorSpace::CALIBRATED_RGB:
          if (aLength >= 104) {
            ReadCalRgbEndpoint(aData, 56, 92, mH.mColorSpace.mCalibrated.mRed);
            ReadCalRgbEndpoint(aData, 68, 96,
                               mH.mColorSpace.mCalibrated.mGreen);
            ReadCalRgbEndpoint(aData, 80, 100,
                               mH.mColorSpace.mCalibrated.mBlue);
          } else {
            mH.mCsType = Nothing();
          }
          break;
        case InfoColorSpace::EMBEDDED:
          if (aLength >= 116) {
            mH.mColorSpace.mProfile.mOffset =
                LittleEndian::readUint32(aData + 108);
            mH.mColorSpace.mProfile.mLength =
                LittleEndian::readUint32(aData + 112);
          } else {
            mH.mCsType = Nothing();
          }
          break;
        case InfoColorSpace::LINKED:
        case InfoColorSpace::SRGB:
        case InfoColorSpace::WIN:
        default:
          // Nothing to be done at this time.
          break;
      }
    }

    // For WinBMPv4, WinBMPv5 and (possibly) OS2-BMPv2 there are additional
    // fields in the info header which we ignore, with the possible exception
    // of the color bitfields (see below).
  }

  // The height for BMPs embedded inside an ICO includes spaces for the AND
  // mask even if it is not present, thus we need to adjust for that here.
  if (mIsWithinICO) {
    // XXX(seth): Should we really be writing the absolute value from
    // the BIH below? Seems like this could be problematic for inverted BMPs.
    mH.mHeight = abs(mH.mHeight) / 2;
  }

  // Run with MOZ_LOG=BMPDecoder:5 set to see this output.
  MOZ_LOG(sBMPLog, LogLevel::Debug,
          ("BMP: bihsize=%u, %d x %d, bpp=%u, compression=%u, colors=%u, "
           "data-offset=%u\n",
           mH.mBIHSize, mH.mWidth, mH.mHeight, uint32_t(mH.mBpp),
           mH.mCompression, mH.mNumColors, mH.mDataOffset));

  // BMPs with negative width are invalid. Also, reject extremely wide images
  // to keep the math sane. And reject INT_MIN as a height because you can't
  // get its absolute value (because -INT_MIN is one more than INT_MAX).
  const int32_t k64KWidth = 0x0000FFFF;
  bool sizeOk =
      0 <= mH.mWidth && mH.mWidth <= k64KWidth && mH.mHeight != INT_MIN;
  if (!sizeOk) {
    return Transition::TerminateFailure();
  }

  // Check mBpp and mCompression.
  bool bppCompressionOk =
      (mH.mCompression == Compression::RGB &&
       (mH.mBpp == 1 || mH.mBpp == 4 || mH.mBpp == 8 || mH.mBpp == 16 ||
        mH.mBpp == 24 || mH.mBpp == 32)) ||
      (mH.mCompression == Compression::RLE8 && mH.mBpp == 8) ||
      (mH.mCompression == Compression::RLE4 && mH.mBpp == 4) ||
      (mH.mCompression == Compression::BITFIELDS &&
       // For BITFIELDS compression we require an exact match for one of the
       // WinBMP BIH sizes; this clearly isn't an OS2 BMP.
       (mH.mBIHSize == InfoHeaderLength::WIN_V3 ||
        mH.mBIHSize == InfoHeaderLength::BITMAPV2INFOHEADER ||
        mH.mBIHSize == InfoHeaderLength::BITMAPV3INFOHEADER ||
        mH.mBIHSize == InfoHeaderLength::WIN_V4 ||
        mH.mBIHSize == InfoHeaderLength::WIN_V5) &&
       (mH.mBpp == 16 || mH.mBpp == 32));
  if (!bppCompressionOk) {
    return Transition::TerminateFailure();
  }

  // Initialize our current row to the top of the image.
  mCurrentRow = AbsoluteHeight();

  // Round it up to the nearest byte count, then pad to 4-byte boundary.
  // Compute this even for a metadate decode because GetCompressedImageSize()
  // relies on it.
  mPixelRowSize = (mH.mBpp * mH.mWidth + 7) / 8;
  uint32_t surplus = mPixelRowSize % 4;
  if (surplus != 0) {
    mPixelRowSize += 4 - surplus;
  }

  if (mIsWithinICO && mH.mCompression == Compression::RGB) {
    // The ICO decoders calls GetCompressedImageSize so we need to make sure the
    // computation it does cannot overflow. Keep this in sync with that
    // function.
    auto product = CheckedInt<uint32_t>(mPixelRowSize) * AbsoluteHeight();
    if (!product.isValid()) {
      return Transition::TerminateFailure();
    }
  }

  size_t bitFieldsLengthStillToRead = 0;
  if (mH.mCompression == Compression::BITFIELDS) {
    // Need to read bitfields.
    if (mH.mBIHSize >= InfoHeaderLength::BITMAPV2INFOHEADER) {
      // Bitfields are embedded in the info header. The 52-byte variant
      // (BITMAPV2INFOHEADER) carries RGB masks only; 56 bytes and larger
      // also include the alpha mask.
      bool hasAlpha = mH.mBIHSize >= InfoHeaderLength::BITMAPV3INFOHEADER;
      mBitFields.ReadFromHeader(aData + 36, hasAlpha);

      // If this came from the clipboard, then we know that even if the header
      // explicitly includes the bitfield masks, we need to add an additional
      // offset for the start of the RGB data.
      if (mIsForClipboard) {
        mH.mDataOffset += BitFields::LENGTH;
      }
    } else {
      // Bitfields are present after the info header, so we will read them in
      // ReadBitfields().
      bitFieldsLengthStillToRead = BitFields::LENGTH;
    }
  } else if (mH.mBpp == 16) {
    // No bitfields specified; use the default 5-5-5 values.
    mBitFields.SetR5G5B5();
  } else if (mH.mBpp == 32) {
    // No bitfields specified; use the default 8-8-8 values.
    mBitFields.SetR8G8B8();
  }

  return Transition::To(State::BITFIELDS, bitFieldsLengthStillToRead);
}

void BitFields::ReadFromHeader(const char* aData, bool aReadAlpha) {
  mRed.Set(LittleEndian::readUint32(aData + 0));
  mGreen.Set(LittleEndian::readUint32(aData + 4));
  mBlue.Set(LittleEndian::readUint32(aData + 8));
  if (aReadAlpha) {
    mAlpha.Set(LittleEndian::readUint32(aData + 12));
  }
}

LexerTransition<nsBMPDecoder::State> nsBMPDecoder::ReadBitfields(
    const char* aData, size_t aLength) {
  mPreGapLength += aLength;

  // If aLength is zero there are no bitfields to read, or we already read them
  // in ReadInfoHeader().
  if (aLength != 0) {
    // Only WinBMPv3-NT puts the bitfield masks here, immediately after the
    // info header. That variant carries RGB masks only -- the alpha mask was
    // not added until later headers (BITMAPV3INFOHEADER and beyond), where the
    // masks live inside the info header itself and are read in
    // ReadInfoHeaderRest().
    mBitFields.ReadFromHeader(aData, /* aReadAlpha = */ false);
  }

  // Note that RLE-encoded BMPs might be transparent because the 'delta' mode
  // can skip pixels and cause implicit transparency.
  mMayHaveTransparency = mIsWithinICO || mH.mCompression == Compression::RLE8 ||
                         mH.mCompression == Compression::RLE4 ||
                         (mH.mCompression == Compression::BITFIELDS &&
                          mBitFields.mAlpha.IsPresent());
  if (mMayHaveTransparency) {
    PostHasTransparency();
  }

  // Post our size to the superclass.
  PostSize(mH.mWidth, AbsoluteHeight());
  if (WantsFrameCount()) {
    PostFrameCount(/* aFrameCount */ 1);
  }
  if (HasError()) {
    return Transition::TerminateFailure();
  }

  // We've now read all the headers. If we're doing a metadata decode, we're
  // done.
  if (IsMetadataDecode()) {
    return Transition::TerminateSuccess();
  }

  // Set up the color table, if present; it'll be filled in by ReadColorTable().
  if (mH.mBpp <= 8) {
    mNumColors = 1 << mH.mBpp;
    if (0 < mH.mNumColors && mH.mNumColors < mNumColors) {
      mNumColors = mH.mNumColors;
    }

    // Always allocate and zero 256 entries, even though mNumColors might be
    // smaller, because the file might erroneously index past mNumColors.
    mColors = MakeUniqueFallible<ColorTableEntry[]>(256);
    if (NS_WARN_IF(!mColors)) {
      return Transition::TerminateFailure();
    }
    memset(mColors.get(), 0, 256 * sizeof(ColorTableEntry));

    // OS/2 Bitmaps have no padding byte.
    mBytesPerColor = (mH.mBIHSize == InfoHeaderLength::WIN_V2) ? 3 : 4;
  }

  if (mCMSMode != CMSMode::Off) {
    bool usesRGB = false;
    if (mH.mCsType.isSome()) {
      switch (*mH.mCsType) {
        case InfoColorSpace::EMBEDDED:
          return SeekColorProfile(aLength);
        case InfoColorSpace::CALIBRATED_RGB:
          PrepareCalibratedColorProfile();
          break;
        case InfoColorSpace::SRGB:
        case InfoColorSpace::WIN:
          MOZ_LOG(sBMPLog, LogLevel::Debug, ("using sRGB color profile\n"));
          usesRGB = true;
          break;
        case InfoColorSpace::LINKED:
        default:
          // Not supported, no color management.
          MOZ_LOG(sBMPLog, LogLevel::Debug,
                  ("color space type not provided\n"));
          // treat as untagged
          if (mCMSMode == CMSMode::All) {
            usesRGB = true;
          }
          break;
      }
    } else if (mCMSMode == CMSMode::All) {
      usesRGB = true;
    }

    if (usesRGB) {
      PreparesRGBTransform();
    }
  }

  return Transition::To(State::ALLOCATE_SURFACE, 0);
}

void nsBMPDecoder::PrepareCalibratedColorProfile() {
  // BMP does not define a white point. Use the same as sRGB. This matches what
  // Chrome does as well.
  qcms_CIE_xyY white_point = qcms_white_point_sRGB();

  qcms_CIE_xyYTRIPLE primaries;
  float redGamma =
      CalRbgEndpointToQcms(mH.mColorSpace.mCalibrated.mRed, primaries.red);
  float greenGamma =
      CalRbgEndpointToQcms(mH.mColorSpace.mCalibrated.mGreen, primaries.green);
  float blueGamma =
      CalRbgEndpointToQcms(mH.mColorSpace.mCalibrated.mBlue, primaries.blue);

  // Explicitly verify the profile because sometimes the values from the BMP
  // header are just garbage.
  mInProfile = qcms_profile_create_rgb_with_gamma_set(
      white_point, primaries, redGamma, greenGamma, blueGamma);
  if (mInProfile && qcms_profile_is_bogus(mInProfile)) {
    // Bad profile, just use sRGB instead. Release the profile here, so that
    // our destructor doesn't assume we are the owner for the transform.
    qcms_profile_release(mInProfile);
    mInProfile = nullptr;
  }

  if (mInProfile) {
    MOZ_LOG(sBMPLog, LogLevel::Debug, ("using calibrated RGB color profile\n"));
    PrepareColorProfileTransform();
  } else if (mCMSMode == CMSMode::All) {
    MOZ_LOG(sBMPLog, LogLevel::Debug,
            ("failed to create calibrated RGB color profile, using sRGB\n"));
    PreparesRGBTransform();
  }
}

void nsBMPDecoder::PrepareColorProfileTransform() {
  if (!mInProfile || !GetCMSOutputProfile()) {
    return;
  }

  qcms_data_type inType;
  qcms_data_type outType;
  if (mColors) {
    // We will transform the color table instead of the output pixels.
    inType = QCMS_DATA_RGB_8;
    outType = QCMS_DATA_RGB_8;
  } else {
    inType = gfxPlatform::GetCMSOSRGBAType();
    outType = inType;
  }

  qcms_intent intent;
  switch (mH.mCsIntent) {
    case InfoColorIntent::BUSINESS:
      intent = QCMS_INTENT_SATURATION;
      break;
    case InfoColorIntent::GRAPHICS:
      intent = QCMS_INTENT_RELATIVE_COLORIMETRIC;
      break;
    case InfoColorIntent::ABS_COLORIMETRIC:
      intent = QCMS_INTENT_ABSOLUTE_COLORIMETRIC;
      break;
    case InfoColorIntent::IMAGES:
    default:
      intent = QCMS_INTENT_PERCEPTUAL;
      break;
  }

  mTransform = qcms_transform_create(mInProfile, inType, GetCMSOutputProfile(),
                                     outType, intent);
  if (!mTransform) {
    MOZ_LOG(sBMPLog, LogLevel::Debug,
            ("failed to create color profile transform\n"));
  }
}

void nsBMPDecoder::PreparesRGBTransform() {
  if (mColors) {
    // We will transform the color table instead of the output pixels.
    mTransform = GetCMSsRGBTransform(SurfaceFormat::R8G8B8);
  } else {
    mTransform = GetCMSsRGBTransform(SurfaceFormat::OS_RGBA);
  }
}

LexerTransition<nsBMPDecoder::State> nsBMPDecoder::SeekColorProfile(
    size_t aLength) {
  // The offset needs to be at least after the color table.
  uint32_t offset = mH.mColorSpace.mProfile.mOffset;
  if (offset <= mH.mBIHSize + aLength + mNumColors * mBytesPerColor ||
      mH.mColorSpace.mProfile.mLength == 0) {
    if (mCMSMode == CMSMode::All) {
      PreparesRGBTransform();
    }

    return Transition::To(State::ALLOCATE_SURFACE, 0);
  }

  // We have already read the header and bitfields.
  offset -= mH.mBIHSize + aLength;

  // We need to skip ahead to search for the embedded color profile. We want
  // to return to this point once we read it.
  MOZ_ASSERT(!mReturnIterator.isSome());
  mReturnIterator = mLexer.Clone(*mIterator, SIZE_MAX);
  if (!mReturnIterator) {
    return Transition::TerminateFailure();
  }

  return Transition::ToUnbuffered(State::FOUND_COLOR_PROFILE,
                                  State::SKIP_TO_COLOR_PROFILE, offset);
}

LexerTransition<nsBMPDecoder::State> nsBMPDecoder::ReadColorProfile(
    const char* aData, size_t aLength) {
  mInProfile = qcms_profile_from_memory(aData, aLength);
  if (mInProfile) {
    MOZ_LOG(sBMPLog, LogLevel::Debug, ("using embedded color profile\n"));
    PrepareColorProfileTransform();
  } else if (mCMSMode == CMSMode::All) {
    PreparesRGBTransform();
  }

  // Jump back to where we left off.
  MOZ_ASSERT(mReturnIterator.isSome());
  mIterator = std::move(mReturnIterator);
  return Transition::To(State::ALLOCATE_SURFACE, 0);
}

LexerTransition<nsBMPDecoder::State> nsBMPDecoder::AllocateSurface() {
  SurfaceFormat format;
  SurfacePipeFlags pipeFlags = SurfacePipeFlags();

  if (mMayHaveTransparency) {
    format = SurfaceFormat::OS_RGBA;
    if (!(GetSurfaceFlags() & SurfaceFlags::NO_PREMULTIPLY_ALPHA)) {
      pipeFlags |= SurfacePipeFlags::PREMULTIPLY_ALPHA;
    }
  } else {
    format = SurfaceFormat::OS_RGBX;
  }

  if (mH.mHeight >= 0) {
    // BMPs store their rows in reverse order, so we may need to flip.
    pipeFlags |= SurfacePipeFlags::FLIP_VERTICALLY;
  }

  mRowBuffer.reset(new (fallible) uint32_t[mH.mWidth]);
  if (!mRowBuffer) {
    return Transition::TerminateFailure();
  }

  // Only give the color transform to the SurfacePipe if we are not transforming
  // the color table in advance.
  qcms_transform* transform = mColors ? nullptr : mTransform;

  Maybe<SurfacePipe> pipe = SurfacePipeFactory::CreateSurfacePipe(
      this, Size(), OutputSize(), FullFrame(), format, format, Nothing(),
      transform, pipeFlags);
  if (!pipe) {
    return Transition::TerminateFailure();
  }

  mPipe = std::move(*pipe);
  ClearRowBufferRemainder();

  // We might want to back track to here if we have a corrupt bmp that points
  // into the color table for image data, so save an iterator at this point.
  MOZ_ASSERT(!mReturnIterator.isSome());
  mReturnIterator = mLexer.Clone(*mIterator, SIZE_MAX);
  if (!mReturnIterator) {
    return Transition::TerminateFailure();
  }

  return Transition::To(State::COLOR_TABLE, mNumColors * mBytesPerColor);
}

LexerTransition<nsBMPDecoder::State> nsBMPDecoder::ReadColorTable(
    const char* aData, size_t aLength) {
  MOZ_ASSERT_IF(aLength != 0, mNumColors > 0 && mColors);

  mPreGapLength += aLength;

  for (uint32_t i = 0; i < mNumColors; i++) {
    // The format is BGR or BGR0.
    mColors[i].mBlue = uint8_t(aData[0]);
    mColors[i].mGreen = uint8_t(aData[1]);
    mColors[i].mRed = uint8_t(aData[2]);
    aData += mBytesPerColor;
  }

  // If we have a color table and a transform, we can avoid transforming each
  // pixel by doing the table in advance. We color manage every entry in the
  // table, even if it is smaller in case the BMP is malformed and overruns
  // its stated color range.
  if (mColors && mTransform) {
    qcms_transform_data(mTransform, mColors.get(), mColors.get(), 256);
  }

  // If we are decoding a BMP from the clipboard, we did not know the data
  // offset in advance. It is just defined as after the header and color table.
  if (mIsForClipboard) {
    mH.mDataOffset += mPreGapLength;
  }

  // We know how many bytes we've read so far (mPreGapLength) and we know the
  // offset of the pixel data (mH.mDataOffset), so we can determine the length
  // of the gap (possibly zero) between the color table and the pixel data.
  //
  // If the gap is negative the file must be malformed (e.g. mH.mDataOffset
  // points into the middle of the color palette instead of past the end).
  if (mPreGapLength > mH.mDataOffset) {
    // Allow corrupt bmp that say the image data starts in the color table, but
    // if the image data offset is even before the color tables thats too far
    // back and give up.
    if (mPreGapLength - aLength > mH.mDataOffset) {
      return Transition::TerminateFailure();
    }
    MOZ_ASSERT(mReturnIterator.isSome());
    mIterator = std::move(mReturnIterator);
    uint32_t gapLength = mH.mDataOffset - (mPreGapLength - aLength);
    return Transition::ToUnbuffered(State::AFTER_GAP, State::GAP, gapLength);
  }

  mReturnIterator.reset();

  uint32_t gapLength = mH.mDataOffset - mPreGapLength;
  return Transition::ToUnbuffered(State::AFTER_GAP, State::GAP, gapLength);
}

LexerTransition<nsBMPDecoder::State> nsBMPDecoder::SkipGap() {
  return Transition::ContinueUnbuffered(State::GAP);
}

LexerTransition<nsBMPDecoder::State> nsBMPDecoder::AfterGap() {
  // If there are no pixels we can stop.
  //
  // XXX: normally, if there are no pixels we will have stopped decoding before
  // now, outside of this decoder. However, if the BMP is within an ICO file,
  // it's possible that the ICO claimed the image had a non-zero size while the
  // BMP claims otherwise. This test is to catch that awkward case. If we ever
  // come up with a more general solution to this ICO-and-BMP-disagree-on-size
  // problem, this test can be removed.
  if (mH.mWidth == 0 || mH.mHeight == 0) {
    return Transition::TerminateSuccess();
  }

  bool hasRLE = mH.mCompression == Compression::RLE8 ||
                mH.mCompression == Compression::RLE4;
  return hasRLE ? Transition::To(State::RLE_SEGMENT, RLE::SEGMENT_LENGTH)
                : Transition::To(State::PIXEL_ROW, mPixelRowSize);
}

LexerTransition<nsBMPDecoder::State> nsBMPDecoder::ReadPixelRow(
    const char* aData) {
  MOZ_ASSERT(mCurrentRow > 0);
  MOZ_ASSERT(mCurrentPos == 0);

  const uint8_t* src = reinterpret_cast<const uint8_t*>(aData);
  uint32_t* dst = RowBuffer();
  uint32_t lpos = mH.mWidth;
  switch (mH.mBpp) {
    case 1:
      while (lpos > 0) {
        int8_t bit;
        uint8_t idx;
        for (bit = 7; bit >= 0 && lpos > 0; bit--) {
          idx = (*src >> bit) & 1;
          SetPixel(dst, idx, mColors);
          --lpos;
        }
        ++src;
      }
      break;

    case 4:
      while (lpos > 0) {
        Set4BitPixel(dst, *src, lpos, mColors);
        ++src;
      }
      break;

    case 8:
      while (lpos > 0) {
        SetPixel(dst, *src, mColors);
        --lpos;
        ++src;
      }
      break;

    case 16:
      if (mBitFields.IsR5G5B5()) {
        // Specialize this common case.
        while (lpos > 0) {
          uint16_t val = LittleEndian::readUint16(src);
          SetPixel(dst, mBitFields.mRed.Get5(val), mBitFields.mGreen.Get5(val),
                   mBitFields.mBlue.Get5(val));
          --lpos;
          src += 2;
        }
      } else {
        bool anyHasAlpha = false;
        while (lpos > 0) {
          uint16_t val = LittleEndian::readUint16(src);
          SetPixel(dst, mBitFields.mRed.Get(val), mBitFields.mGreen.Get(val),
                   mBitFields.mBlue.Get(val),
                   mBitFields.mAlpha.GetAlpha(val, anyHasAlpha));
          --lpos;
          src += 2;
        }
        if (anyHasAlpha) {
          MOZ_ASSERT(mMayHaveTransparency);
          mDoesHaveTransparency = true;
        }
      }
      break;

    case 24:
      while (lpos > 0) {
        SetPixel(dst, src[2], src[1], src[0]);
        --lpos;
        src += 3;
      }
      break;

    case 32:
      if (mH.mCompression == Compression::RGB && mIsWithinICO &&
          mH.mBpp == 32) {
        // This is a special case only used for 32bpp WinBMPv3-ICO files, which
        // could be in either 0RGB or ARGB format. We start by assuming it's
        // an 0RGB image. If we hit a non-zero alpha value, then we know it's
        // actually an ARGB image, and change tack accordingly.
        // (Note: a fully-transparent ARGB image is indistinguishable from a
        // 0RGB image, and we will render such an image as a 0RGB image, i.e.
        // opaquely. This is unlikely to be a problem in practice.)
        while (lpos > 0) {
          if (!mDoesHaveTransparency && src[3] != 0) {
            // Up until now this looked like an 0RGB image, but we now know
            // it's actually an ARGB image. Which means every pixel we've seen
            // so far has been fully transparent. So we go back and redo them.

            // Tell the SurfacePipe to go back to the start.
            mPipe.ResetToFirstRow();

            // Redo the complete rows we've already done.
            MOZ_ASSERT(mCurrentPos == 0);
            int32_t currentRow = mCurrentRow;
            mCurrentRow = AbsoluteHeight();
            ClearRowBufferRemainder();
            while (mCurrentRow > currentRow) {
              FinishRow();
            }

            // Reset the row pointer back to where we started.
            dst = RowBuffer() + (mH.mWidth - lpos);

            MOZ_ASSERT(mMayHaveTransparency);
            mDoesHaveTransparency = true;
          }

          // If mDoesHaveTransparency is false, treat this as an 0RGB image.
          // Otherwise, treat this as an ARGB image.
          SetPixel(dst, src[2], src[1], src[0],
                   mDoesHaveTransparency ? src[3] : 0xff);
          src += 4;
          --lpos;
        }
      } else if (mBitFields.IsR8G8B8()) {
        // Specialize this common case.
        while (lpos > 0) {
          uint32_t val = LittleEndian::readUint32(src);
          SetPixel(dst, mBitFields.mRed.Get8(val), mBitFields.mGreen.Get8(val),
                   mBitFields.mBlue.Get8(val));
          --lpos;
          src += 4;
        }
      } else {
        bool anyHasAlpha = false;
        while (lpos > 0) {
          uint32_t val = LittleEndian::readUint32(src);
          SetPixel(dst, mBitFields.mRed.Get(val), mBitFields.mGreen.Get(val),
                   mBitFields.mBlue.Get(val),
                   mBitFields.mAlpha.GetAlpha(val, anyHasAlpha));
          --lpos;
          src += 4;
        }
        if (anyHasAlpha) {
          MOZ_ASSERT(mMayHaveTransparency);
          mDoesHaveTransparency = true;
        }
      }
      break;

    default:
      MOZ_CRASH("Unsupported color depth; earlier check didn't catch it?");
  }

  FinishRow();
  return mCurrentRow == 0 ? Transition::TerminateSuccess()
                          : Transition::To(State::PIXEL_ROW, mPixelRowSize);
}

LexerTransition<nsBMPDecoder::State> nsBMPDecoder::ReadRLESegment(
    const char* aData) {
  if (mCurrentRow == 0) {
    return Transition::TerminateSuccess();
  }

  uint8_t byte1 = uint8_t(aData[0]);
  uint8_t byte2 = uint8_t(aData[1]);

  if (byte1 != RLE::ESCAPE) {
    // Encoded mode consists of two bytes: byte1 specifies the number of
    // consecutive pixels to be drawn using the color index contained in
    // byte2.
    //
    // Work around bitmaps that specify too many pixels.
    uint32_t pixelsNeeded = std::min<uint32_t>(mH.mWidth - mCurrentPos, byte1);
    if (pixelsNeeded) {
      uint32_t* dst = RowBuffer();
      mCurrentPos += pixelsNeeded;
      if (mH.mCompression == Compression::RLE8) {
        do {
          SetPixel(dst, byte2, mColors);
          pixelsNeeded--;
        } while (pixelsNeeded);
      } else {
        do {
          Set4BitPixel(dst, byte2, pixelsNeeded, mColors);
        } while (pixelsNeeded);
      }
    }
    return Transition::To(State::RLE_SEGMENT, RLE::SEGMENT_LENGTH);
  }

  if (byte2 == RLE::ESCAPE_EOL) {
    ClearRowBufferRemainder();
    mCurrentPos = 0;
    FinishRow();
    return mCurrentRow == 0
               ? Transition::TerminateSuccess()
               : Transition::To(State::RLE_SEGMENT, RLE::SEGMENT_LENGTH);
  }

  if (byte2 == RLE::ESCAPE_EOF) {
    return Transition::TerminateSuccess();
  }

  if (byte2 == RLE::ESCAPE_DELTA) {
    return Transition::To(State::RLE_DELTA, RLE::DELTA_LENGTH);
  }

  // Absolute mode. |byte2| gives the number of pixels. The length depends on
  // whether it's 4-bit or 8-bit RLE. Also, the length must be even (and zero
  // padding is used to achieve this when necessary).
  MOZ_ASSERT(mAbsoluteModeNumPixels == 0);
  mAbsoluteModeNumPixels = byte2;
  uint32_t length = byte2;
  if (mH.mCompression == Compression::RLE4) {
    length = (length + 1) / 2;  // halve, rounding up
  }
  if (length & 1) {
    length++;
  }
  return Transition::To(State::RLE_ABSOLUTE, length);
}

LexerTransition<nsBMPDecoder::State> nsBMPDecoder::ReadRLEDelta(
    const char* aData) {
  // Delta encoding makes it possible to skip pixels making part of the image
  // transparent.
  MOZ_ASSERT(mMayHaveTransparency);
  mDoesHaveTransparency = true;

  // Clear the skipped pixels. (This clears to the end of the row,
  // which is perfect if there's a Y delta and harmless if not).
  ClearRowBufferRemainder();

  // Handle the XDelta.
  mCurrentPos += uint8_t(aData[0]);
  if (mCurrentPos > mH.mWidth) {
    mCurrentPos = mH.mWidth;
  }

  // Handle the Y Delta.
  int32_t yDelta = std::min<int32_t>(uint8_t(aData[1]), mCurrentRow);
  if (yDelta > 0) {
    // Commit the current row (the first of the skipped rows).
    FinishRow();

    // Clear and commit the remaining skipped rows. We want to be careful not
    // to change mCurrentPos here.
    memset(mRowBuffer.get(), 0, mH.mWidth * sizeof(uint32_t));
    for (int32_t line = 1; line < yDelta; line++) {
      FinishRow();
    }
  }

  return mCurrentRow == 0
             ? Transition::TerminateSuccess()
             : Transition::To(State::RLE_SEGMENT, RLE::SEGMENT_LENGTH);
}

LexerTransition<nsBMPDecoder::State> nsBMPDecoder::ReadRLEAbsolute(
    const char* aData, size_t aLength) {
  uint32_t n = mAbsoluteModeNumPixels;
  mAbsoluteModeNumPixels = 0;

  if (mCurrentPos + n > uint32_t(mH.mWidth)) {
    // Some DIB RLE8 encoders count a padding byte as the absolute mode
    // pixel number at the end of the row.
    if (mH.mCompression == Compression::RLE8 && n > 0 && (n & 1) == 0 &&
        mCurrentPos + n - uint32_t(mH.mWidth) == 1 && aLength > 0 &&
        aData[aLength - 1] == 0) {
      n--;
    } else {
      // Bad data. Stop decoding; at least part of the image may have been
      // decoded.
      return Transition::TerminateSuccess();
    }
  }

  // In absolute mode, n represents the number of pixels that follow, each of
  // which contains the color index of a single pixel.
  uint32_t* dst = RowBuffer();
  uint32_t iSrc = 0;
  uint32_t* oldPos = dst;
  if (mH.mCompression == Compression::RLE8) {
    while (n > 0) {
      SetPixel(dst, aData[iSrc], mColors);
      n--;
      iSrc++;
    }
  } else {
    while (n > 0) {
      Set4BitPixel(dst, aData[iSrc], n, mColors);
      iSrc++;
    }
  }
  mCurrentPos += dst - oldPos;

  // We should read all the data (unless the last byte is zero padding).
  MOZ_ASSERT(iSrc == aLength - 1 || iSrc == aLength);

  return Transition::To(State::RLE_SEGMENT, RLE::SEGMENT_LENGTH);
}

}  // namespace image
}  // namespace mozilla
