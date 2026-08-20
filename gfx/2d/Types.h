/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_TYPES_H_
#define MOZILLA_GFX_TYPES_H_

#include <stddef.h>
#include <stdint.h>

#include <bit>
#include <iosfwd>  // for ostream
#include <optional>

#include "mozilla/DefineEnum.h"  // for MOZ_DEFINE_ENUM_CLASS_WITH_BASE
#include "mozilla/EnumeratedRange.h"
#include "mozilla/MacroArgs.h"  // for MOZ_CONCAT
#include "mozilla/Maybe.h"
#include "mozilla/TypedEnumBits.h"

namespace mozilla {
namespace gfx {

typedef float Float;
typedef double Double;

enum class SurfaceType : int8_t {
  DATA,                   /* Data surface - bitmap in memory */
  CAIRO,                  /* Surface wrapping a cairo surface */
  CAIRO_IMAGE,            /* Data surface wrapping a cairo image surface */
  COREGRAPHICS_IMAGE,     /* Surface wrapping a CoreGraphics Image */
  COREGRAPHICS_CGCONTEXT, /* Surface wrapping a CG context */
  SKIA,                   /* Surface wrapping a Skia bitmap */
  RECORDING,              /* Surface used for recording */
  CANVAS_RECORDING,       /* Surface used for canvas recording */
  DATA_SHARED,            /* Data surface using shared memory */
  DATA_RECYCLING_SHARED,  /* Data surface using shared memory */
  OFFSET,                 /* Offset */
  DATA_ALIGNED,           /* Data surface using aligned heap memory */
  DATA_SHARED_WRAPPER,    /* Shared memory mapped in from another process */
  BLOB_IMAGE,             /* Recorded blob image */
  DATA_MAPPED,            /* Data surface wrapping a ScopedMap */
  WEBGL,                  /* Surface wrapping a DrawTargetWebgl texture */
  D3D11_TEXTURE,          /* Surface wrapping a D3D11Texture */
};

enum class SurfaceFormat : int8_t {
  // The following values are named to reflect layout of colors in memory, from
  // lowest byte to highest byte. The 32-bit value layout depends on machine
  // endianness.
  //               in-memory            32-bit LE value   32-bit BE value
  B8G8R8A8,  // [BB, GG, RR, AA]     0xAARRGGBB        0xBBGGRRAA
  B8G8R8X8,  // [BB, GG, RR, 00]     0x00RRGGBB        0xBBGGRR00
  R8G8B8A8,  // [RR, GG, BB, AA]     0xAABBGGRR        0xRRGGBBAA
  R8G8B8X8,  // [RR, GG, BB, 00]     0x00BBGGRR        0xRRGGBB00
  A8R8G8B8,  // [AA, RR, GG, BB]     0xBBGGRRAA        0xAARRGGBB
  X8R8G8B8,  // [00, RR, GG, BB]     0xBBGGRR00        0x00RRGGBB

  R8G8B8,
  B8G8R8,

  // The _UINT16 suffix here indicates that the name reflects the layout when
  // viewed as a uint16_t value. In memory these values are stored using native
  // endianness.
  R5G6B5_UINT16,  // 0bRRRRRGGGGGGBBBBB

  // This one is a single-byte, so endianness isn't an issue.
  A8,
  A16,

  R8G8,
  R16G16,

  // These ones are their own special cases.
  YUV420,     // Sometimes called YU12. 3 planes of 8 bit Y, then Cb, then Cr.
              // 4:2:0 chroma subsampling.
  YUV420P10,  // YUV420 but with 16 bit plane values where the 6 most
              // significant bits are 0 (so it's 10-bit format).
  YUV422P10,  // 3 planes like YUV420, but with 4:2:2 chroma subampling and
              // 16 bit plane values where the 6 least significant bits are 0.
  NV12,       // 2 planes. YUV 4:2:0 image with a plane of 8 bit Y samples
              // followed by an interleaved U/V plane containing 8 bit 2x2
              // subsampled colour difference samples.
  P016,       // Similar to NV12, but with 16 bits plane values
  P010,       // Identical to P016 but the 6 least significant bits are 0.
              // With DXGI in theory entirely compatible, however practice has
              // shown that it's not the case.
  NV16,       // Similar to NV12, but with 4:2:2 chroma subsampling.
  P210,       // Similar to P010, but with 4:2:2 chroma subsampling.
  YUY2,       // Sometimes called YUYV. Single plane / packed YUV 4:2:2 8 bit
              // samples interleaved as Y`0 Cb Y`1 Cr. Since 4 pixels require
              // 64 bits, this can also be considered a 16bpp format, but each
              // component is only 8 bits. We sometimes pack RGBA data into
              // this format.
  HSV,
  Lab,
  Depth,

  // LE packed 10bit per channel format primarily associated with HDR10 video.
  R10G10B10A2_UINT32,  // 0bAARRRRRRRRRRGGGGGGGGGGBBBBBBBBBB
  // Same as R10G10B10A2_UINT32 but with the alpha channel ignored.
  R10G10B10X2_UINT32,  // 0b00RRRRRRRRRRGGGGGGGGGGBBBBBBBBBB
  // 4 half-float (f16) components in RGBA order for HDR rendering, each is
  // machine endian.
  R16G16B16A16F,

  CMYK,
  InvertedCMYK,

  // This represents the unknown format.
  UNKNOWN,  // TODO: Replace uses with Maybe<SurfaceFormat>.

  // The following values are endian-independent synonyms. The _UINT32 suffix
  // indicates that the name reflects the layout when viewed as a uint32_t
  // value.
  A8R8G8B8_UINT32 = std::endian::native == std::endian::little
                        ? B8G8R8A8
                        : A8R8G8B8,  // 0xAARRGGBB
  X8R8G8B8_UINT32 = std::endian::native == std::endian::little
                        ? B8G8R8X8
                        : X8R8G8B8,  // 0x00RRGGBB

  // The following values are OS and endian-independent synonyms.
  //
  // TODO(aosmond): When everything blocking bug 1581828 has been resolved, we
  // can make this use R8B8G8A8 and R8B8G8X8 for non-Windows platforms.
  OS_RGBA = A8R8G8B8_UINT32,
  OS_RGBX = X8R8G8B8_UINT32
};

enum class SubpixelOrder : uint8_t {
  UNKNOWN,
  RGB,
  BGR,
  VRGB,
  VBGR,
};

struct SurfaceFormatInfo {
  bool hasColor;
  bool hasAlpha;
  bool isYuv;
  std::optional<uint8_t> bytesPerPixel;
};
inline std::optional<SurfaceFormatInfo> Info(const SurfaceFormat aFormat) {
  auto info = SurfaceFormatInfo{};

  switch (aFormat) {
    case SurfaceFormat::B8G8R8A8:
    case SurfaceFormat::R8G8B8A8:
    case SurfaceFormat::A8R8G8B8:
    case SurfaceFormat::R10G10B10A2_UINT32:
    case SurfaceFormat::R16G16B16A16F:
      info.hasColor = true;
      info.hasAlpha = true;
      break;

    case SurfaceFormat::B8G8R8X8:
    case SurfaceFormat::R8G8B8X8:
    case SurfaceFormat::X8R8G8B8:
    case SurfaceFormat::R8G8B8:
    case SurfaceFormat::B8G8R8:
    case SurfaceFormat::R5G6B5_UINT16:
    case SurfaceFormat::R10G10B10X2_UINT32:
    case SurfaceFormat::R8G8:
    case SurfaceFormat::R16G16:
    case SurfaceFormat::HSV:
    case SurfaceFormat::Lab:
      info.hasColor = true;
      info.hasAlpha = false;
      break;

    case SurfaceFormat::A8:
    case SurfaceFormat::A16:
      info.hasColor = false;
      info.hasAlpha = true;
      break;

    case SurfaceFormat::YUV420:
    case SurfaceFormat::YUV420P10:
    case SurfaceFormat::YUV422P10:
    case SurfaceFormat::NV12:
    case SurfaceFormat::P016:
    case SurfaceFormat::P010:
    case SurfaceFormat::NV16:
    case SurfaceFormat::P210:
    case SurfaceFormat::YUY2:
      info.hasColor = true;
      info.hasAlpha = false;
      info.isYuv = true;
      break;

    case SurfaceFormat::Depth:
      info.hasColor = false;
      info.hasAlpha = false;
      info.isYuv = false;
      break;

    case SurfaceFormat::CMYK:
    case SurfaceFormat::InvertedCMYK:
      info.hasColor = true;
      info.hasAlpha = false;
      break;

    case SurfaceFormat::UNKNOWN:
      break;
  }

  // -
  // bytesPerPixel

  switch (aFormat) {
    case SurfaceFormat::B8G8R8A8:
    case SurfaceFormat::R8G8B8A8:
    case SurfaceFormat::A8R8G8B8:
    case SurfaceFormat::B8G8R8X8:
    case SurfaceFormat::R8G8B8X8:
    case SurfaceFormat::X8R8G8B8:
    case SurfaceFormat::R16G16:
    case SurfaceFormat::CMYK:
    case SurfaceFormat::InvertedCMYK:
      info.bytesPerPixel = 4;
      break;

    case SurfaceFormat::R8G8B8:
    case SurfaceFormat::B8G8R8:
      info.bytesPerPixel = 3;
      break;

    case SurfaceFormat::R5G6B5_UINT16:
    case SurfaceFormat::R8G8:
    case SurfaceFormat::A16:
    case SurfaceFormat::Depth:  // uint16_t
      info.bytesPerPixel = 2;
      break;

    case SurfaceFormat::A8:
      info.bytesPerPixel = 1;
      break;

    case SurfaceFormat::R10G10B10A2_UINT32:
    case SurfaceFormat::R10G10B10X2_UINT32:
      info.bytesPerPixel = 4;
      break;

    case SurfaceFormat::R16G16B16A16F:
      info.bytesPerPixel = 8;
      break;

    case SurfaceFormat::HSV:
    case SurfaceFormat::Lab:
      info.bytesPerPixel = 3 * sizeof(float);
      break;

    case SurfaceFormat::YUV420:
    case SurfaceFormat::YUV420P10:
    case SurfaceFormat::YUV422P10:
    case SurfaceFormat::NV12:
    case SurfaceFormat::P016:
    case SurfaceFormat::P010:
    case SurfaceFormat::NV16:
    case SurfaceFormat::P210:
    case SurfaceFormat::YUY2:
    case SurfaceFormat::UNKNOWN:
      break;  // No bytesPerPixel per se.
  }

  // -

  if (aFormat == SurfaceFormat::UNKNOWN) {
    return {};
  }
  return info;
}

std::ostream& operator<<(std::ostream& aOut, const SurfaceFormat& aFormat);

// Represents the bit-shifts required to access color channels when the layout
// is viewed as a uint32_t value.
enum class SurfaceFormatBit : uint32_t {
  R8G8B8A8_R = std::endian::native == std::endian::little ? 0 : 24,
  R8G8B8A8_G = std::endian::native == std::endian::little ? 8 : 16,
  R8G8B8A8_B = std::endian::native == std::endian::little ? 16 : 8,
  R8G8B8A8_A = std::endian::native == std::endian::little ? 24 : 0,

  // The following values are endian-independent for A8R8G8B8_UINT32.
  A8R8G8B8_UINT32_B = 0,
  A8R8G8B8_UINT32_G = 8,
  A8R8G8B8_UINT32_R = 16,
  A8R8G8B8_UINT32_A = 24,

  // The following values are OS and endian-independent.
  //
  // TODO(aosmond): When everything blocking bug 1581828 has been resolved, we
  // can make this use R8G8B8A8_X for non-Windows platforms.
  OS_R = A8R8G8B8_UINT32_R,
  OS_G = A8R8G8B8_UINT32_G,
  OS_B = A8R8G8B8_UINT32_B,
  OS_A = A8R8G8B8_UINT32_A,
};

inline uint32_t operator<<(uint8_t a, SurfaceFormatBit b) {
  return a << static_cast<uint32_t>(b);
}

inline uint32_t operator>>(uint32_t a, SurfaceFormatBit b) {
  return a >> static_cast<uint32_t>(b);
}

static inline int BytesPerPixel(SurfaceFormat aFormat) {
  // TODO: return Info(aFormat).value().bytesPerPixel.value();
  switch (aFormat) {
    case SurfaceFormat::A8:
      return 1;
    case SurfaceFormat::R5G6B5_UINT16:
    case SurfaceFormat::A16:
      return 2;
    case SurfaceFormat::R8G8B8:
    case SurfaceFormat::B8G8R8:
      return 3;
    case SurfaceFormat::HSV:
    case SurfaceFormat::Lab:
      return 3 * sizeof(float);
    case SurfaceFormat::Depth:
      return sizeof(uint16_t);
    case SurfaceFormat::B8G8R8A8:
    case SurfaceFormat::B8G8R8X8:
    case SurfaceFormat::R8G8B8A8:
    case SurfaceFormat::R8G8B8X8:
    case SurfaceFormat::A8R8G8B8:
    case SurfaceFormat::X8R8G8B8:
    case SurfaceFormat::R10G10B10A2_UINT32:
    case SurfaceFormat::R10G10B10X2_UINT32:
    case SurfaceFormat::R16G16:
    case SurfaceFormat::CMYK:
    case SurfaceFormat::InvertedCMYK:
      return 4;
    case SurfaceFormat::R16G16B16A16F:
      return 8;
    case SurfaceFormat::R8G8:
      return 2;
    case SurfaceFormat::YUV420:
    case SurfaceFormat::YUV420P10:
    case SurfaceFormat::YUV422P10:
    case SurfaceFormat::NV12:
    case SurfaceFormat::NV16:
    case SurfaceFormat::YUY2:
      // These formats are not easily described in terms of bytes per pixel,
      // technically 1.5 bytes per pixel on average, which is guaranteed by the
      // width and height being multiples of 2.
      return 0;
    case SurfaceFormat::P016:
    case SurfaceFormat::P010:
    case SurfaceFormat::P210:
      // Similar to NV12 but uint16 pixels.
      return 0;
    case SurfaceFormat::UNKNOWN:
      MOZ_ASSERT_UNREACHABLE("unhandled gfx::SurfaceFormat::UNKNOWN");
      return 4;
  }
  MOZ_ASSERT_UNREACHABLE("unhandled enum value for gfx::SurfaceFormat");
  return 4;
}

inline bool IsOpaque(SurfaceFormat aFormat) {
  // TODO: return Info(aFormat).value().hasAlpha;
  switch (aFormat) {
    case SurfaceFormat::B8G8R8X8:
    case SurfaceFormat::R8G8B8X8:
    case SurfaceFormat::X8R8G8B8:
    case SurfaceFormat::R5G6B5_UINT16:
    case SurfaceFormat::R10G10B10X2_UINT32:
    case SurfaceFormat::R8G8B8:
    case SurfaceFormat::B8G8R8:
    case SurfaceFormat::R8G8:
    case SurfaceFormat::HSV:
    case SurfaceFormat::Lab:
    case SurfaceFormat::Depth:
    case SurfaceFormat::YUV420:
    case SurfaceFormat::NV12:
    case SurfaceFormat::P010:
    case SurfaceFormat::P016:
    case SurfaceFormat::NV16:
    case SurfaceFormat::P210:
    case SurfaceFormat::YUY2:
    case SurfaceFormat::CMYK:
    case SurfaceFormat::InvertedCMYK:
      return true;
    case SurfaceFormat::B8G8R8A8:
    case SurfaceFormat::R8G8B8A8:
    case SurfaceFormat::A8R8G8B8:
    case SurfaceFormat::R10G10B10A2_UINT32:
    case SurfaceFormat::R16G16B16A16F:
    case SurfaceFormat::A8:
    case SurfaceFormat::A16:
    case SurfaceFormat::R16G16:
    case SurfaceFormat::YUV420P10:
    case SurfaceFormat::YUV422P10:
    case SurfaceFormat::UNKNOWN:
      return false;
  }
  MOZ_ASSERT_UNREACHABLE("unhandled enum value for gfx::SurfaceFormat");
  return false;
}

// These are standardized Coding-independent Code Points
// See [Rec. ITU-T H.273
// (12/2016)](https://www.itu.int/rec/T-REC-H.273-201612-I/en)
//
// We deliberately use an unscoped enum with fixed uint8_t representation since
// all possible values [0, 255] are legal, but it's unwieldy to declare 200+
// "RESERVED" enumeration values. Having a fixed underlying type avoids any
// potential UB and avoids the need for a cast when passing these values across
// FFI to functions like qcms_profile_create_cicp.
namespace CICP {
enum ColourPrimaries : uint8_t {
  CP_RESERVED_MIN = 0,  // 0, 3, [13, 21], [23, 255] are all reserved
  CP_BT709 = 1,
  CP_UNSPECIFIED = 2,
  CP_BT470M = 4,
  CP_BT470BG = 5,
  CP_BT601 = 6,
  CP_SMPTE240 = 7,
  CP_GENERIC_FILM = 8,
  CP_BT2020 = 9,
  CP_XYZ = 10,
  CP_SMPTE431 = 11,
  CP_SMPTE432 = 12,
  CP_EBU3213 = 22,
};

inline bool IsReserved(ColourPrimaries aIn) {
  switch (aIn) {
    case CP_BT709:
    case CP_UNSPECIFIED:
    case CP_BT470M:
    case CP_BT470BG:
    case CP_BT601:
    case CP_SMPTE240:
    case CP_GENERIC_FILM:
    case CP_BT2020:
    case CP_XYZ:
    case CP_SMPTE431:
    case CP_SMPTE432:
    case CP_EBU3213:
      return false;
    default:
      return true;
  }
}

enum TransferCharacteristics : uint8_t {
  TC_RESERVED_MIN = 0,  // 0, 3, [19, 255] are all reserved
  TC_BT709 = 1,
  TC_UNSPECIFIED = 2,
  TC_BT470M = 4,
  TC_BT470BG = 5,
  TC_BT601 = 6,
  TC_SMPTE240 = 7,
  TC_LINEAR = 8,
  TC_LOG_100 = 9,
  TC_LOG_100_SQRT10 = 10,
  TC_IEC61966 = 11,
  TC_BT_1361 = 12,
  TC_SRGB = 13,
  TC_BT2020_10BIT = 14,
  TC_BT2020_12BIT = 15,
  TC_SMPTE2084 = 16,
  TC_SMPTE428 = 17,
  TC_HLG = 18,
};

inline bool IsReserved(TransferCharacteristics aIn) {
  switch (aIn) {
    case TC_BT709:
    case TC_UNSPECIFIED:
    case TC_BT470M:
    case TC_BT470BG:
    case TC_BT601:
    case TC_SMPTE240:
    case TC_LINEAR:
    case TC_LOG_100:
    case TC_LOG_100_SQRT10:
    case TC_IEC61966:
    case TC_BT_1361:
    case TC_SRGB:
    case TC_BT2020_10BIT:
    case TC_BT2020_12BIT:
    case TC_SMPTE2084:
    case TC_SMPTE428:
    case TC_HLG:
      return false;
    default:
      return true;
  }
}

enum MatrixCoefficients : uint8_t {
  MC_IDENTITY = 0,
  MC_BT709 = 1,
  MC_UNSPECIFIED = 2,
  MC_RESERVED_MIN = 3,  // 3, [15, 255] are all reserved
  MC_FCC = 4,
  MC_BT470BG = 5,
  MC_BT601 = 6,
  MC_SMPTE240 = 7,
  MC_YCGCO = 8,
  MC_BT2020_NCL = 9,
  MC_BT2020_CL = 10,
  MC_SMPTE2085 = 11,
  MC_CHROMAT_NCL = 12,
  MC_CHROMAT_CL = 13,
  MC_ICTCP = 14,
};

inline bool IsReserved(MatrixCoefficients aIn) {
  switch (aIn) {
    case MC_IDENTITY:
    case MC_BT709:
    case MC_UNSPECIFIED:
    case MC_RESERVED_MIN:
    case MC_FCC:
    case MC_BT470BG:
    case MC_BT601:
    case MC_SMPTE240:
    case MC_YCGCO:
    case MC_BT2020_NCL:
    case MC_BT2020_CL:
    case MC_SMPTE2085:
    case MC_CHROMAT_NCL:
    case MC_CHROMAT_CL:
    case MC_ICTCP:
      return false;
    default:
      return true;
  }
}
}  // namespace CICP

// The matrix coeffiecients used for YUV to RGB conversion.
enum class YUVColorSpace : uint8_t {
  BT601,
  BT709,
  BT2020,
  Identity,  // Todo: s/YUVColorSpace/ColorSpace/, s/Identity/SRGB/
  Default = BT709,
  _First = BT601,
  _Last = Identity,
};

enum class ColorDepth : uint8_t {
  COLOR_8,
  COLOR_10,
  COLOR_12,
  COLOR_16,
  _First = COLOR_8,
  _Last = COLOR_16,
};

std::ostream& operator<<(std::ostream& aOut, const ColorDepth& aColorDepth);

enum class TransferFunction : uint8_t {
  // BT709 is the common SDR video transfer function, because BT709 only defines
  // an OETF, this actually represents BT1886 and BT2020 which add an EOTF.
  // https://en.wikipedia.org/wiki/ITU-R_BT.1886
  // https://en.wikipedia.org/wiki/Rec._2020
  BT709,
  // sRGB is the common SDR web content transfer function, there is some dispute
  // as to how this standard is meant to be interpreted as it was made to encode
  // incoming light (OETF) in a way that is aesthetically pleasing on a CRT in
  // a brightly lit office environment (which can be considered an EOTF), many
  // modern displays display sRGB content using a plain 2.4 gamma mimicking a
  // CRT of that era without regard to the piecewise gamma defined in sRGB.
  //
  // There is much debate about how to interpret this standard, this is just one
  // of the more common interpretations, and is the one that matches BT1886,
  // which also describes analog CRT behavior so seems a good point of
  // reference.
  //
  // This can be used with RGBA16F surfaces to preserve sRGB blending behavior
  // expected in CSS on the web, while still allowing for HDR and wide color
  // gamut content, but it has surprising behaviors such as negative colors and
  // colors up to 65530 (10100 cd/m^2, assuming 1.0 is 100 cd/m^2), for Canvas
  // rendering it's preferred to use LINEAR with RGBA16F however.
  //
  // https://en.wikipedia.org/wiki/SRGB
  SRGB,
  // Perceptual Quantizer, popularized as HDR10, used for both YUV (P010) and
  // RGB (RGB10A2) formats, requires 10bit pixel formats to avoid visible
  // banding.
  // https://en.wikipedia.org/wiki/Perceptual_quantizer
  PQ,
  // Hybrid Log-Gamma, used for both HDR YUV and RGB formats, especially on
  // broadcast HDR content that is meant to be backward compatible with SDR
  // displays in a usable way, can be reasonably used with 8bit (BGRA8, NV12,
  // YV12) or 10bit pixel formats (RGB10A2, P010).
  // https://en.wikipedia.org/wiki/Hybrid_log%E2%80%93gamma
  HLG,
  // Linear transfer function for HDR Canvas and WebRender surfaces with
  // RGBA16F format, can be used for high dynamic range and wide color gamut
  // even on colorspaces that do not otherwise support that (e.g. SRGB),
  // when paired with sRGB this is often called scRGB or srgb-linear.
  LINEAR,
  _First = BT709,
  _Last = LINEAR,
  Default = BT709,
};

enum class ColorRange : uint8_t {
  LIMITED,
  FULL,
  _First = LIMITED,
  _Last = FULL,
};

// HDR metadata structures, populated from codec-level signalling.
struct Chromaticity {
  float x = 0.0f;
  float y = 0.0f;
  bool operator==(const Chromaticity&) const = default;
};

// SMPTE ST 2086 mastering display colour volume.
struct Smpte2086Metadata {
  Chromaticity displayPrimaryRed;
  Chromaticity displayPrimaryGreen;
  Chromaticity displayPrimaryBlue;
  Chromaticity whitePoint;
  float maxLuminance = 0.0f;  // cd/m^2
  float minLuminance = 0.0f;  // cd/m^2
  bool operator==(const Smpte2086Metadata&) const = default;
};

// CTA-861.3 content light level (MaxCLL/MaxFALL).
struct ContentLightLevel {
  uint16_t maxContentLightLevel = 0;
  uint16_t maxFrameAverageLightLevel = 0;
  bool operator==(const ContentLightLevel&) const = default;
};

// HDR metadata signalled at container or codec level. Each field is
// independently optional since streams may carry one without the other.
struct HDRMetadata {
  Maybe<Smpte2086Metadata> mSmpte2086;
  Maybe<ContentLightLevel> mContentLightLevel;
  bool operator==(const HDRMetadata&) const = default;
  bool IsValid() const {
    return mSmpte2086.isSome() || mContentLightLevel.isSome();
  }
};

// Really "YcbcrColorColorSpace" but YUV is the common parlance. This represents
// the combination of matrix coefficients and color range (studio or full) for
// YUV formats. The color primaries are inferred to have the same name as the
// matrix coefficients. The transfer function is assumed for the first set of
// enum values, but the transfer function is explicit in the name of the enum
// value for the last set of enum values. This enum is currently only used for
// video.
enum class YUVRangedColorSpace : uint8_t {
  // BT601 and BT709 use BT709 transfer function in common usage (however on
  // Windows the DXGI colorspace enums conflate sRGB and BT709, which is
  // interesting to think about, both have an EOTF identical to BT1886 but their
  // OETF differs significantly).
  BT601_Narrow = 0,
  BT601_Full,
  BT709_Narrow,
  BT709_Full,
  // BT2020 is used with TransferFunction::BT709 but isn't quite the same, where
  // BT2020 at 12bit+ precision uses its own constants for a more precise fit
  // than BT709 defines.  In practice all video is 8bit or 10bit, so BT709 is a
  // reasonable way to think about it for canonicalization purposes.
  BT2020_Narrow,
  BT2020_Full,
  // BT2100 shares the same primaries with BT2020 but implies the content is HDR
  // so the transfer function is explicitly either HLG (1000 nits) or PQ (10000)
  BT2100_HLG_Narrow,
  BT2100_HLG_Full,
  // Windows supports this only for YUV pixel formats
  BT2100_PQ_Narrow,
  // Windows supports this only for RGB pixel formats
  BT2100_PQ_Full,
  GbrIdentity,

  _First = BT601_Narrow,
  _Last = GbrIdentity,
  Default = BT709_Narrow,
};

// ColorSpace2 defines what color primaries are used for a given surface, this
// is used alongside a TransferFunction and in case of YUV also a ColorRange.
enum class ColorSpace2 : uint8_t {
  // Display color space matches a physical display (usually primary), this is
  // going away in favor of more explicit color management of surfaces.
  Display,
  UNKNOWN = Display,  // We feel sufficiently bad about this TODO.
  // sRGB color primaries, this is the web standard from the beginning, and is
  // expected to be paired with TransferFunction::SRGB or
  // TransferFunction::LINEAR (which some call srgb-linear or scRGB).
  SRGB,
  // Display P3 color primaries, used primarily by macOS/iOS devices, this is
  // typically paired with TransferFunction::SRGB, but could be linear too.
  DISPLAY_P3,
  // SMPTE C 170M NTSC color primaries, used with BT709 transfer function.
  BT601_525,
  // ITU-R BT.709 HDTV color primaries, used with BT709 transfer function.
  BT709,
  // ITU-R BT.601 PAL/SECAM color primaries, used with BT709 transfer function
  // Preserving a note here: Basically BT709, just Xg is 0.290 not 0.300.
  BT601_625 = BT709,
  // ITU-R BT.2020 color primaries, used with BT709 transfer function (except in
  // case of 12bit or higher precision in which case BT2020 defines a more
  // precise set of constants for the BT709 gamma function), or HLG or PQ
  // transfer functions for HDR content (BT2100).
  BT2020,
  _First = Display,
  _Last = BT2020,
};

inline ColorSpace2 ToColorSpace2(const YUVColorSpace in) {
  switch (in) {
    case YUVColorSpace::BT601:
      return ColorSpace2::BT601_525;
    case YUVColorSpace::BT709:
      return ColorSpace2::BT709;
    case YUVColorSpace::BT2020:
      return ColorSpace2::BT2020;
    case YUVColorSpace::Identity:
      return ColorSpace2::SRGB;
  }
  MOZ_ASSERT_UNREACHABLE();
}

inline YUVColorSpace ToYUVColorSpace(const ColorSpace2 in) {
  switch (in) {
    case ColorSpace2::BT601_525:
      return YUVColorSpace::BT601;
    case ColorSpace2::BT709:
      return YUVColorSpace::BT709;
    case ColorSpace2::BT2020:
      return YUVColorSpace::BT2020;
    case ColorSpace2::SRGB:
      return YUVColorSpace::Identity;

    case ColorSpace2::UNKNOWN:
    case ColorSpace2::DISPLAY_P3:
      MOZ_CRASH("Bad ColorSpace2 for ToYUVColorSpace");
  }
  MOZ_ASSERT_UNREACHABLE();
}

struct FromYUVRangedColorSpaceT final {
  const YUVColorSpace space;
  const ColorRange range;
  const TransferFunction transferFunction;
};

inline FromYUVRangedColorSpaceT FromYUVRangedColorSpace(
    const YUVRangedColorSpace s) {
  switch (s) {
    case YUVRangedColorSpace::BT601_Narrow:
      return {YUVColorSpace::BT601, ColorRange::LIMITED,
              TransferFunction::BT709};
    case YUVRangedColorSpace::BT601_Full:
      return {YUVColorSpace::BT601, ColorRange::FULL, TransferFunction::BT709};

    case YUVRangedColorSpace::BT709_Narrow:
      return {YUVColorSpace::BT709, ColorRange::LIMITED,
              TransferFunction::BT709};
    case YUVRangedColorSpace::BT709_Full:
      return {YUVColorSpace::BT709, ColorRange::FULL, TransferFunction::BT709};

    case YUVRangedColorSpace::BT2020_Narrow:
      return {YUVColorSpace::BT2020, ColorRange::LIMITED,
              TransferFunction::BT709};
    case YUVRangedColorSpace::BT2020_Full:
      return {YUVColorSpace::BT2020, ColorRange::FULL, TransferFunction::BT709};
    case YUVRangedColorSpace::BT2100_HLG_Narrow:
      return {YUVColorSpace::BT2020, ColorRange::LIMITED,
              TransferFunction::HLG};
    case YUVRangedColorSpace::BT2100_HLG_Full:
      return {YUVColorSpace::BT2020, ColorRange::FULL, TransferFunction::HLG};
    case YUVRangedColorSpace::BT2100_PQ_Narrow:
      return {YUVColorSpace::BT2020, ColorRange::LIMITED, TransferFunction::PQ};
    case YUVRangedColorSpace::BT2100_PQ_Full:
      return {YUVColorSpace::BT2020, ColorRange::FULL, TransferFunction::PQ};

    case YUVRangedColorSpace::GbrIdentity:
      return {YUVColorSpace::Identity, ColorRange::FULL,
              TransferFunction::BT709};
  }
  MOZ_CRASH("bad YUVRangedColorSpace");
}

inline YUVRangedColorSpace ToYUVRangedColorSpace(
    const YUVColorSpace space, const ColorRange range,
    const gfx::TransferFunction transferFunction) {
  bool narrow;
  switch (range) {
    case ColorRange::FULL:
      narrow = false;
      break;
    case ColorRange::LIMITED:
      narrow = true;
      break;
  }

  switch (space) {
    case YUVColorSpace::Identity:
      MOZ_ASSERT(range == ColorRange::FULL);
      return YUVRangedColorSpace::GbrIdentity;

    case YUVColorSpace::BT601:
      return narrow ? YUVRangedColorSpace::BT601_Narrow
                    : YUVRangedColorSpace::BT601_Full;

    case YUVColorSpace::BT709:
      return narrow ? YUVRangedColorSpace::BT709_Narrow
                    : YUVRangedColorSpace::BT709_Full;

    case YUVColorSpace::BT2020:
      switch (transferFunction) {
        case gfx::TransferFunction::PQ:
          return narrow ? YUVRangedColorSpace::BT2100_PQ_Narrow
                        : YUVRangedColorSpace::BT2100_PQ_Full;
        case gfx::TransferFunction::HLG:
          return narrow ? YUVRangedColorSpace::BT2100_HLG_Narrow
                        : YUVRangedColorSpace::BT2100_HLG_Full;
        case gfx::TransferFunction::SRGB:
          return narrow ? YUVRangedColorSpace::BT2020_Narrow
                        : YUVRangedColorSpace::BT2020_Full;
        case gfx::TransferFunction::BT709:
          return narrow ? YUVRangedColorSpace::BT2020_Narrow
                        : YUVRangedColorSpace::BT2020_Full;
        default:
          MOZ_CRASH("bad TransferFunction for BT2020");
      }
  }
  MOZ_CRASH("bad YUVColorSpace");
}

template <typename DescriptorT>
inline YUVRangedColorSpace GetYUVRangedColorSpace(const DescriptorT& d) {
  return ToYUVRangedColorSpace(d.yUVColorSpace(), d.colorRange(),
                               d.transferFunction());
}

static inline SurfaceFormat SurfaceFormatForColorDepth(ColorDepth aColorDepth) {
  SurfaceFormat format = SurfaceFormat::A8;
  switch (aColorDepth) {
    case ColorDepth::COLOR_8:
      break;
    case ColorDepth::COLOR_10:
    case ColorDepth::COLOR_12:
    case ColorDepth::COLOR_16:
      format = SurfaceFormat::A16;
      break;
  }
  return format;
}

static inline uint8_t BitDepthForColorDepth(ColorDepth aColorDepth) {
  uint8_t depth = 8;
  switch (aColorDepth) {
    case ColorDepth::COLOR_8:
      break;
    case ColorDepth::COLOR_10:
      depth = 10;
      break;
    case ColorDepth::COLOR_12:
      depth = 12;
      break;
    case ColorDepth::COLOR_16:
      depth = 16;
      break;
  }
  return depth;
}

static inline ColorDepth ColorDepthForBitDepth(uint8_t aBitDepth) {
  ColorDepth depth = ColorDepth::COLOR_8;
  switch (aBitDepth) {
    case 8:
      break;
    case 10:
      depth = ColorDepth::COLOR_10;
      break;
    case 12:
      depth = ColorDepth::COLOR_12;
      break;
    case 16:
      depth = ColorDepth::COLOR_16;
      break;
  }
  return depth;
}

// 10 and 12 bits color depth image are using 16 bits integers for storage.
// Data is placed as MSB and texture is sampled within [0 - 1] range.
static inline uint32_t RescalingFactorForColorDepth(ColorDepth aColorDepth) {
  uint32_t factor = 1;
  switch (aColorDepth) {
    case ColorDepth::COLOR_8:
      break;
    case ColorDepth::COLOR_10:
      break;
    case ColorDepth::COLOR_12:
      break;
    case ColorDepth::COLOR_16:
      break;
  }
  return factor;
}

static inline bool IsHDRTransferFunction(
    gfx::TransferFunction aTransferFunction) {
  switch (aTransferFunction) {
    case gfx::TransferFunction::PQ:
    case gfx::TransferFunction::HLG:
    case gfx::TransferFunction::LINEAR:
      return true;
    case gfx::TransferFunction::BT709:
    case gfx::TransferFunction::SRGB:
      return false;
  }
  MOZ_CRASH("bad TransferFunction");
}

enum class ChromaSubsampling : uint8_t {
  FULL,
  HALF_WIDTH,
  HALF_WIDTH_AND_HEIGHT,
  _First = FULL,
  _Last = HALF_WIDTH_AND_HEIGHT,
};

template <typename T>
static inline T ChromaSize(const T& aYSize, ChromaSubsampling aSubsampling) {
  switch (aSubsampling) {
    case ChromaSubsampling::FULL:
      return aYSize;
    case ChromaSubsampling::HALF_WIDTH:
      return T((aYSize.width + 1) / 2, aYSize.height);
    case ChromaSubsampling::HALF_WIDTH_AND_HEIGHT:
      return T((aYSize.width + 1) / 2, (aYSize.height + 1) / 2);
  }
  MOZ_CRASH("bad ChromaSubsampling");
}

enum class FilterType : int8_t {
  BLEND = 0,
  TRANSFORM,
  MORPHOLOGY,
  COLOR_MATRIX,
  FLOOD,
  TILE,
  TABLE_TRANSFER,
  DISCRETE_TRANSFER,
  LINEAR_TRANSFER,
  GAMMA_TRANSFER,
  CONVOLVE_MATRIX,
  DISPLACEMENT_MAP,
  TURBULENCE,
  ARITHMETIC_COMBINE,
  COMPOSITE,
  DIRECTIONAL_BLUR,
  GAUSSIAN_BLUR,
  POINT_DIFFUSE,
  POINT_SPECULAR,
  SPOT_DIFFUSE,
  SPOT_SPECULAR,
  DISTANT_DIFFUSE,
  DISTANT_SPECULAR,
  CROP,
  PREMULTIPLY,
  UNPREMULTIPLY,
  OPACITY
};

enum class DrawTargetType : int8_t {
  SOFTWARE_RASTER = 0,
  HARDWARE_RASTER,
  VECTOR
};

enum class BackendType : int8_t {
  NONE = 0,
  CAIRO,
  SKIA,
  RECORDING,
  WEBRENDER_TEXT,
  WEBGL,

  // Add new entries above this line.
  BACKEND_LAST
};

enum class RecorderType : int8_t {
  UNKNOWN,
  PRIVATE,
  MEMORY,
  CANVAS,
  PRFILEDESC,
  WEBRENDER
};

enum class FontType : int8_t { DWRITE, MAC, FONTCONFIG, FREETYPE, UNKNOWN };

enum class NativeSurfaceType : int8_t {
  D3D10_TEXTURE,
  CAIRO_CONTEXT,
  CGCONTEXT,
  CGCONTEXT_ACCELERATED,
  OPENGL_TEXTURE,
  WEBGL_CONTEXT
};

enum class FontStyle : int8_t { NORMAL, ITALIC, BOLD, BOLD_ITALIC };

enum class FontHinting : int8_t { NONE, LIGHT, NORMAL, FULL };

enum class CompositionOp : int8_t {
  OP_CLEAR,
  OP_OVER,
  OP_ADD,
  OP_ATOP,
  OP_OUT,
  OP_IN,
  OP_SOURCE,
  OP_DEST_IN,
  OP_DEST_OUT,
  OP_DEST_OVER,
  OP_DEST_ATOP,
  OP_XOR,
  OP_MULTIPLY,
  OP_SCREEN,
  OP_OVERLAY,
  OP_DARKEN,
  OP_LIGHTEN,
  OP_COLOR_DODGE,
  OP_COLOR_BURN,
  OP_HARD_LIGHT,
  OP_SOFT_LIGHT,
  OP_DIFFERENCE,
  OP_EXCLUSION,
  OP_HUE,
  OP_SATURATION,
  OP_COLOR,
  OP_LUMINOSITY,
  OP_COUNT
};

enum class Axis : int8_t { X_AXIS, Y_AXIS, BOTH };

enum class ExtendMode : int8_t {
  CLAMP,     // Do not repeat
  REPEAT,    // Repeat in both axis
  REPEAT_X,  // Only X axis
  REPEAT_Y,  // Only Y axis
  REFLECT    // Mirror the image
};

enum class FillRule : int8_t { FILL_WINDING, FILL_EVEN_ODD };

enum class AntialiasMode : int8_t { NONE, GRAY, SUBPIXEL, DEFAULT };

// See https://en.wikipedia.org/wiki/Texture_filtering
enum class SamplingFilter : int8_t {
  GOOD,
  LINEAR,
  POINT,
  SENTINEL  // one past the last valid value
};

std::ostream& operator<<(std::ostream& aOut, const SamplingFilter& aFilter);

// clang-format off
MOZ_DEFINE_ENUM_CLASS_WITH_BASE(PatternType, int8_t, (
  COLOR,
  SURFACE,
  LINEAR_GRADIENT,
  RADIAL_GRADIENT,
  CONIC_GRADIENT
));
// clang-format on

enum class JoinStyle : int8_t {
  BEVEL,
  ROUND,
  MITER,  //!< Mitered if within the miter limit, else, if the backend supports
          //!< it, the miter is clamped. If the backend does not support miter
          //!< clamping the behavior is as for MITER_OR_BEVEL.
  MITER_OR_BEVEL  //!< Mitered if within the miter limit, else beveled.
};

enum class CapStyle : int8_t { BUTT, ROUND, SQUARE };

enum class SamplingBounds : int8_t { UNBOUNDED, BOUNDED };

// Moz2d version for SVG mask types
enum class LuminanceType : int8_t {
  LUMINANCE,
  LINEARRGB,
};

/* Color is stored in non-premultiplied form in sRGB color space */
struct sRGBColor {
 public:
  constexpr sRGBColor() : r(0.0f), g(0.0f), b(0.0f), a(0.0f) {}
  constexpr sRGBColor(Float aR, Float aG, Float aB, Float aA)
      : r(aR), g(aG), b(aB), a(aA) {}
  constexpr sRGBColor(Float aR, Float aG, Float aB)
      : r(aR), g(aG), b(aB), a(1.0f) {}

  static constexpr sRGBColor White(float aA) {
    return sRGBColor(1.f, 1.f, 1.f, aA);
  }

  static constexpr sRGBColor Black(float aA) {
    return sRGBColor(0.f, 0.f, 0.f, aA);
  }

  static constexpr sRGBColor OpaqueWhite() { return White(1.f); }

  static constexpr sRGBColor OpaqueBlack() { return Black(1.f); }

  static constexpr sRGBColor FromU8(uint8_t aR, uint8_t aG, uint8_t aB,
                                    uint8_t aA) {
    return sRGBColor(float(aR) / 255.f, float(aG) / 255.f, float(aB) / 255.f,
                     float(aA) / 255.f);
  }

  static constexpr sRGBColor FromABGR(uint32_t aColor) {
    return sRGBColor(((aColor >> 0) & 0xff) * (1.0f / 255.0f),
                     ((aColor >> 8) & 0xff) * (1.0f / 255.0f),
                     ((aColor >> 16) & 0xff) * (1.0f / 255.0f),
                     ((aColor >> 24) & 0xff) * (1.0f / 255.0f));
  }

  // The "Unusual" prefix is to avoid unintentionally using this function when
  // FromABGR(), which is much more common, is needed.
  static constexpr sRGBColor UnusualFromARGB(uint32_t aColor) {
    return sRGBColor(((aColor >> 16) & 0xff) * (1.0f / 255.0f),
                     ((aColor >> 8) & 0xff) * (1.0f / 255.0f),
                     ((aColor >> 0) & 0xff) * (1.0f / 255.0f),
                     ((aColor >> 24) & 0xff) * (1.0f / 255.0f));
  }

  constexpr uint32_t ToABGR() const {
    return uint32_t(r * 255.0f) | uint32_t(g * 255.0f) << 8 |
           uint32_t(b * 255.0f) << 16 | uint32_t(a * 255.0f) << 24;
  }

  constexpr sRGBColor Premultiplied() const {
    return sRGBColor(r * a, g * a, b * a, a);
  }

  constexpr sRGBColor Unpremultiplied() const {
    return a > 0.f ? sRGBColor(r / a, g / a, b / a, a) : *this;
  }

  // The "Unusual" prefix is to avoid unintentionally using this function when
  // ToABGR(), which is much more common, is needed.
  uint32_t UnusualToARGB() const {
    return uint32_t(b * 255.0f) | uint32_t(g * 255.0f) << 8 |
           uint32_t(r * 255.0f) << 16 | uint32_t(a * 255.0f) << 24;
  }

  bool operator==(const sRGBColor& aColor) const {
    return r == aColor.r && g == aColor.g && b == aColor.b && a == aColor.a;
  }

  bool operator!=(const sRGBColor& aColor) const { return !(*this == aColor); }

  Float r, g, b, a;
};

/* Color is stored in non-premultiplied form in device color space */
struct DeviceColor {
 public:
  constexpr DeviceColor() : r(0.0f), g(0.0f), b(0.0f), a(0.0f) {}
  constexpr DeviceColor(Float aR, Float aG, Float aB, Float aA)
      : r(aR), g(aG), b(aB), a(aA) {}
  constexpr DeviceColor(Float aR, Float aG, Float aB)
      : r(aR), g(aG), b(aB), a(1.0f) {}

  /* The following Mask* variants are helpers used to make it clear when a
   * particular color is being used for masking purposes. These masks should
   * never be colored managed. */
  static DeviceColor Mask(float aC, float aA) {
    return DeviceColor(aC, aC, aC, aA);
  }

  static DeviceColor MaskWhite(float aA) { return Mask(1.f, aA); }

  static DeviceColor MaskBlack(float aA) { return Mask(0.f, aA); }

  static DeviceColor MaskOpaqueWhite() { return MaskWhite(1.f); }

  static DeviceColor MaskOpaqueBlack() { return MaskBlack(1.f); }

  static DeviceColor FromU8(uint8_t aR, uint8_t aG, uint8_t aB, uint8_t aA) {
    return DeviceColor(float(aR) / 255.f, float(aG) / 255.f, float(aB) / 255.f,
                       float(aA) / 255.f);
  }

  static DeviceColor FromABGR(uint32_t aColor) {
    DeviceColor newColor(((aColor >> 0) & 0xff) * (1.0f / 255.0f),
                         ((aColor >> 8) & 0xff) * (1.0f / 255.0f),
                         ((aColor >> 16) & 0xff) * (1.0f / 255.0f),
                         ((aColor >> 24) & 0xff) * (1.0f / 255.0f));

    return newColor;
  }

  // The "Unusual" prefix is to avoid unintentionally using this function when
  // FromABGR(), which is much more common, is needed.
  static DeviceColor UnusualFromARGB(uint32_t aColor) {
    DeviceColor newColor(((aColor >> 16) & 0xff) * (1.0f / 255.0f),
                         ((aColor >> 8) & 0xff) * (1.0f / 255.0f),
                         ((aColor >> 0) & 0xff) * (1.0f / 255.0f),
                         ((aColor >> 24) & 0xff) * (1.0f / 255.0f));

    return newColor;
  }

  uint32_t ToABGR() const {
    return uint32_t(r * 255.0f) | uint32_t(g * 255.0f) << 8 |
           uint32_t(b * 255.0f) << 16 | uint32_t(a * 255.0f) << 24;
  }

  // The "Unusual" prefix is to avoid unintentionally using this function when
  // ToABGR(), which is much more common, is needed.
  uint32_t UnusualToARGB() const {
    return uint32_t(b * 255.0f) | uint32_t(g * 255.0f) << 8 |
           uint32_t(r * 255.0f) << 16 | uint32_t(a * 255.0f) << 24;
  }

  bool operator==(const DeviceColor& aColor) const {
    return r == aColor.r && g == aColor.g && b == aColor.b && a == aColor.a;
  }

  bool operator!=(const DeviceColor& aColor) const {
    return !(*this == aColor);
  }

  friend std::ostream& operator<<(std::ostream& aOut,
                                  const DeviceColor& aColor);

  Float r, g, b, a;
};

struct GradientStop {
  bool operator<(const GradientStop& aOther) const {
    return offset < aOther.offset;
  }

  Float offset;
  DeviceColor color;
};

enum class JobStatus { Complete, Wait, Yield, Error };

enum class DeviceResetReason {
  OK = 0,        // No reset.
  HUNG,          // Windows specific, guilty device reset.
  REMOVED,       // Windows specific, device removed or driver upgraded.
  RESET,         // Guilty device reset.
  DRIVER_ERROR,  // Innocent device reset.
  INVALID_CALL,  // Windows specific, guilty device reset.
  OUT_OF_MEMORY,
  FORCED_RESET,  // Simulated device reset.
  OTHER,         // Unrecognized reason for device reset.
  NVIDIA_VIDEO,  // Linux specific, NVIDIA video memory was reset.
  UNKNOWN,       // GL specific, unknown if guilty or innocent.
  _First = OK,
  _Last = UNKNOWN,
};

enum class DeviceResetDetectPlace {
  WR_BEGIN_FRAME = 0,
  WR_WAIT_FOR_GPU,
  WR_POST_UPDATE,
  WR_SYNC_OBJRCT,
  WR_SIMULATE,
  WIDGET,
  CANVAS_TRANSLATOR,
  WR_BEFORE_READBACK,
  _First = WR_BEGIN_FRAME,
  _Last = WR_BEFORE_READBACK,
};

enum class ForcedDeviceResetReason {
  OPENSHAREDHANDLE = 0,
  COMPOSITOR_UPDATED,
};

}  // namespace gfx
}  // namespace mozilla

// XXX: temporary
typedef mozilla::gfx::SurfaceFormat gfxImageFormat;

#if defined(XP_WIN) && defined(MOZ_GFX)
#  ifdef GFX2D_INTERNAL
#    define GFX2D_API __declspec(dllexport)
#  else
#    define GFX2D_API __declspec(dllimport)
#  endif
#else
#  define GFX2D_API
#endif

namespace mozilla {

// Side constants for use in various places.
enum Side : uint8_t { eSideTop, eSideRight, eSideBottom, eSideLeft };

std::ostream& operator<<(std::ostream&, const mozilla::Side&);

constexpr auto AllPhysicalSides() {
  return mozilla::MakeInclusiveEnumeratedRange(eSideTop, eSideLeft);
}

enum class SideBits {
  eNone = 0,
  eTop = 1 << eSideTop,
  eRight = 1 << eSideRight,
  eBottom = 1 << eSideBottom,
  eLeft = 1 << eSideLeft,
  eTopBottom = SideBits::eTop | SideBits::eBottom,
  eLeftRight = SideBits::eLeft | SideBits::eRight,
  eAll = SideBits::eTopBottom | SideBits::eLeftRight
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(SideBits)

inline constexpr SideBits SideToSideBit(mozilla::Side aSide) {
  return SideBits(1 << aSide);
}

enum Corner : uint8_t {
  // This order is important!
  eCornerTopLeft = 0,
  eCornerTopRight = 1,
  eCornerBottomRight = 2,
  eCornerBottomLeft = 3
};

// RectCornerRadii::radii depends on this value. It is not being added to
// Corner because we want to lift the responsibility to handle it in the
// switch-case.
constexpr int eCornerCount = 4;

constexpr auto AllPhysicalCorners() {
  return mozilla::MakeInclusiveEnumeratedRange(eCornerTopLeft,
                                               eCornerBottomLeft);
}

// Indices into "half corner" arrays (nsStyleCorners e.g.)
enum HalfCorner : uint8_t {
  // This order is important!
  eCornerTopLeftX = 0,
  eCornerTopLeftY = 1,
  eCornerTopRightX = 2,
  eCornerTopRightY = 3,
  eCornerBottomRightX = 4,
  eCornerBottomRightY = 5,
  eCornerBottomLeftX = 6,
  eCornerBottomLeftY = 7
};

constexpr auto AllPhysicalHalfCorners() {
  return mozilla::MakeInclusiveEnumeratedRange(eCornerTopLeftX,
                                               eCornerBottomLeftY);
}

// The result of these conversion functions are exhaustively checked in
// nsFrame.cpp, which also serves as usage examples.

constexpr bool HalfCornerIsX(HalfCorner aHalfCorner) {
  return !(aHalfCorner % 2);
}

constexpr Corner HalfToFullCorner(HalfCorner aHalfCorner) {
  return Corner(aHalfCorner / 2);
}

constexpr HalfCorner FullToHalfCorner(Corner aCorner, bool aIsVertical) {
  return HalfCorner(aCorner * 2 + aIsVertical);
}

constexpr bool SideIsVertical(mozilla::Side aSide) { return aSide % 2; }

// @param aIsSecond when true, return the clockwise second of the two
// corners associated with aSide. For example, with aSide = eSideBottom the
// result is eCornerBottomRight when aIsSecond is false, and
// eCornerBottomLeft when aIsSecond is true.
constexpr Corner SideToFullCorner(mozilla::Side aSide, bool aIsSecond) {
  return Corner((aSide + aIsSecond) % 4);
}

// @param aIsSecond see SideToFullCorner.
// @param aIsParallel return the half-corner that is parallel with aSide
// when aIsParallel is true. For example with aSide=eSideTop, aIsSecond=true
// the result is eCornerTopRightX when aIsParallel is true, and
// eCornerTopRightY when aIsParallel is false (because "X" is parallel with
// eSideTop/eSideBottom, similarly "Y" is parallel with
// eSideLeft/eSideRight)
constexpr HalfCorner SideToHalfCorner(mozilla::Side aSide, bool aIsSecond,
                                      bool aIsParallel) {
  return HalfCorner(((aSide + aIsSecond) * 2 + (aSide + !aIsParallel) % 2) % 8);
}

}  // namespace mozilla

#endif /* MOZILLA_GFX_TYPES_H_ */
