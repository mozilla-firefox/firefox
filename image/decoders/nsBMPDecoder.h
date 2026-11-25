/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_decoders_nsBMPDecoder_h
#define mozilla_image_decoders_nsBMPDecoder_h

#include "BMPHeaders.h"
#include "Decoder.h"
#include "StreamingLexer.h"
#include "SurfacePipe.h"
#include "gfxColor.h"
#include "mozilla/UniquePtr.h"

namespace mozilla {
namespace image {

namespace bmp {

struct CalRgbEndpoint {
  uint32_t mGamma;
  uint32_t mX;
  uint32_t mY;
  uint32_t mZ;
};

/// This struct contains the fields from the file header and info header that
/// we use during decoding. (Excluding bitfields fields, which are kept in
/// BitFields.)
struct Header {
  uint32_t mDataOffset;           // Offset to raster data.
  uint32_t mBIHSize;              // Header size.
  int32_t mWidth;                 // Image width.
  int32_t mHeight;                // Image height.
  uint16_t mBpp;                  // Bits per pixel.
  uint32_t mCompression;          // See struct Compression for valid values.
  uint32_t mImageSize;            // (compressed) image size. Can be 0 if
                                  // mCompression==0.
  uint32_t mNumColors;            // Used colors.
  Maybe<InfoColorSpace> mCsType;  // Color space type.
  InfoColorIntent mCsIntent;      // Color space intent.

  union {
    struct {
      CalRgbEndpoint mRed;
      CalRgbEndpoint mGreen;
      CalRgbEndpoint mBlue;
    } mCalibrated;

    struct {
      uint32_t mOffset;
      uint32_t mLength;
    } mProfile;
  } mColorSpace;

  Header()
      : mDataOffset(0),
        mBIHSize(0),
        mWidth(0),
        mHeight(0),
        mBpp(0),
        mCompression(0),
        mImageSize(0),
        mNumColors(0),
        mCsIntent(InfoColorIntent::IMAGES) {}
};

/// An entry in the color table.
struct ColorTableEntry {
  uint8_t mRed;
  uint8_t mGreen;
  uint8_t mBlue;
};

/// All the color-related bitfields for 16bpp and 32bpp images. We use this
/// even for older format BMPs that don't have explicit bitfields.
class BitFields {
  class Value {
    friend class BitFields;

    uint32_t mMask;       // The mask for the value.
    uint8_t mRightShift;  // The amount to right-shift after masking.
    uint8_t mBitWidth;    // The width (in bits) of the value.

    /// Sets the mask (and thus the right-shift and bit-width as well).
    void Set(uint32_t aMask);

   public:
    Value() {
      mMask = 0;
      mRightShift = 0;
      mBitWidth = 0;
    }

    /// Returns true if this channel is used. Only used for alpha.
    bool IsPresent() const { return mMask != 0x0; }

    /// Extracts the single color value from the multi-color value.
    uint8_t Get(uint32_t aVal) const;

    /// Like Get(), but specially for alpha.
    uint8_t GetAlpha(uint32_t aVal, bool& aHasAlphaOut) const;

    /// Specialized versions of Get() for when the bit-width is 5 or 8.
    /// (They will assert if called and the bit-width is not 5 or 8.)
    uint8_t Get5(uint32_t aVal) const;
    uint8_t Get8(uint32_t aVal) const;
  };

 public:
  /// The individual color channels.
  Value mRed;
  Value mGreen;
  Value mBlue;
  Value mAlpha;

  /// Set bitfields to the standard 5-5-5 16bpp values.
  void SetR5G5B5();

  /// Set bitfields to the standard 8-8-8 32bpp values.
  void SetR8G8B8();

  /// Test if bitfields have the standard 5-5-5 16bpp values.
  bool IsR5G5B5() const;

  /// Test if bitfields have the standard 8-8-8 32bpp values.
  bool IsR8G8B8() const;

  /// Read the bitfields from a header. The reading of the alpha mask is
  /// optional.
  void ReadFromHeader(const char* aData, bool aReadAlpha);

  /// Length of the bitfields structure in the BMP file.
  static const size_t LENGTH = 12;
};

}  // namespace bmp

class RasterImage;

/// Decoder for BMP-Files, as used by Windows and OS/2.

class nsBMPDecoder : public Decoder {
 public:
  ~nsBMPDecoder();

  DecoderType GetType() const override { return DecoderType::BMP; }

  /// @return true if this BMP is a valid ICO resource.
  bool IsValidICOResource() const override { return true; }

  /// Obtains the internal output image buffer.
  uint32_t* GetImageData() { return reinterpret_cast<uint32_t*>(mImageData); }

  /// Obtains the length of the internal output image buffer.
  size_t GetImageDataLength() const { return mImageDataLength; }

  /// Obtains the size of the compressed image resource.
  uint32_t GetCompressedImageSize() const;

  /// Mark this BMP as being within an ICO file. Only used for testing purposes
  /// because the ICO-specific constructor does this marking automatically.
  void SetIsWithinICO() { mIsWithinICO = true; }

  /// Did the BMP file have alpha data of any kind? (Only use this after the
  /// bitmap has been fully decoded.)
  bool HasTransparency() const { return mDoesHaveTransparency; }

  LexerResult DoDecode(SourceBufferIterator& aIterator,
                       IResumable* aOnResume) override;
  nsresult BeforeFinishInternal() override;
  nsresult FinishInternal() override;

 private:
  friend class DecoderFactory;

  enum class State {
    FILE_HEADER,
    INFO_HEADER_SIZE,
    INFO_HEADER_REST,
    BITFIELDS,
    SKIP_TO_COLOR_PROFILE,
    FOUND_COLOR_PROFILE,
    COLOR_PROFILE,
    ALLOCATE_SURFACE,
    COLOR_TABLE,
    GAP,
    AFTER_GAP,
    PIXEL_ROW,
    RLE_SEGMENT,
    RLE_DELTA,
    RLE_ABSOLUTE
  };

  // This is the constructor used for normal and clipboard BMP images.
  explicit nsBMPDecoder(RasterImage* aImage, bool aForClipboard = false);

  // This is the constructor used for BMP resources in ICO images.
  nsBMPDecoder(RasterImage* aImage, uint32_t aDataOffset);

  // Helper constructor called by the other two.
  nsBMPDecoder(RasterImage* aImage, State aState, size_t aLength,
               bool aForClipboard);

  uint32_t AbsoluteHeight() const { return abs(mH.mHeight); }

  uint32_t* RowBuffer();
  void ClearRowBufferRemainder();

  void FinishRow();

  void PrepareCalibratedColorProfile();
  void PrepareColorProfileTransform();
  void PreparesRGBTransform();

  LexerTransition<State> ReadFileHeader(const char* aData, size_t aLength);
  LexerTransition<State> ReadInfoHeaderSize(const char* aData, size_t aLength);
  LexerTransition<State> ReadInfoHeaderRest(const char* aData, size_t aLength);
  LexerTransition<State> ReadBitfields(const char* aData, size_t aLength);
  LexerTransition<State> SeekColorProfile(size_t aLength);
  LexerTransition<State> ReadColorProfile(const char* aData, size_t aLength);
  LexerTransition<State> AllocateSurface();
  LexerTransition<State> ReadColorTable(const char* aData, size_t aLength);
  LexerTransition<State> SkipGap();
  LexerTransition<State> AfterGap();
  LexerTransition<State> ReadPixelRow(const char* aData);
  LexerTransition<State> ReadRLESegment(const char* aData);
  LexerTransition<State> ReadRLEDelta(const char* aData);
  LexerTransition<State> ReadRLEAbsolute(const char* aData, size_t aLength);

  SurfacePipe mPipe;

  StreamingLexer<State> mLexer;

  // Iterator to save return point.
  Maybe<SourceBufferIterator> mReturnIterator;

  UniquePtr<uint32_t[]> mRowBuffer;

  bmp::Header mH;

  // If the BMP is within an ICO file our treatment of it differs slightly.
  bool mIsWithinICO;

  // If the BMP decoded from the clipboard, we don't start with a data offset.
  bool mIsForClipboard;

  bmp::BitFields mBitFields;

  // Might the image have transparency? Determined from the headers during
  // metadata decode. (Does not guarantee the image actually has transparency.)
  bool mMayHaveTransparency;

  // Does the image have transparency? Determined during full decoding, so only
  // use this after that has been completed.
  bool mDoesHaveTransparency;

  uint32_t mNumColors;  // The number of used colors, i.e. the number of
                        // entries in mColors, if it's present.
  UniquePtr<bmp::ColorTableEntry[]>
      mColors;              // The color table, if it's present.
  uint32_t mBytesPerColor;  // 3 or 4, depending on the format

  // The number of bytes prior to the optional gap that have been read. This
  // is used to find the start of the pixel data.
  uint32_t mPreGapLength;

  uint32_t mPixelRowSize;  // The number of bytes per pixel row.

  int32_t mCurrentRow;  // Index of the row of the image that's currently
                        // being decoded: [height,1].
  int32_t mCurrentPos;  // Index into the current line. Used when
                        // doing RLE decoding and when filling in pixels
                        // for truncated files.

  // Only used in RLE_ABSOLUTE state: the number of pixels to read.
  uint32_t mAbsoluteModeNumPixels;
};

}  // namespace image
}  // namespace mozilla

#endif  // mozilla_image_decoders_nsBMPDecoder_h
