/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef DOM_MEDIA_PLATFORMS_MEDIACODECSSUPPORT_H_
#define DOM_MEDIA_PLATFORMS_MEDIACODECSSUPPORT_H_
#include <array>

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/EnumSet.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/gfx/Types.h"
#include "nsString.h"
#include "nsTHashMap.h"
#include "nsThreadUtils.h"

namespace mozilla::media {
// List of codecs we support, used in the below macros
// to generate MediaCodec and MediaCodecSupports enums.
#define CODEC_LIST \
  X(H264)          \
  X(VP8)           \
  X(VP9)           \
  X(AV1)           \
  X(HEVC)          \
  X(AAC)           \
  X(FLAC)          \
  X(MP3)           \
  X(Opus)          \
  X(Vorbis)        \
  X(Wave)

// Generate MediaCodec enum with the names of each codec we support.
// Example: MediaCodec::H264
enum class MediaCodec : int {
#define X(name) name,
  CODEC_LIST
#undef X
      SENTINEL
};
using MediaCodecSet = EnumSet<MediaCodec, uint64_t>;

// Helper macros used to create codec-specific SW/HW de/encode enums below.
#define SW_DECODE(codec) codec##SoftwareDecode
#define HW_DECODE(codec) codec##HardwareDecode
#define SW_ENCODE(codec) codec##SoftwareEncode
#define HW_ENCODE(codec) codec##HardwareEncode

// For codec which we can do hardware de/encoding once user installs the free
// platform extension, eg. AV1 on Windows
#define LACK_HW_EXTENSION(codec) codec##LackOfExtension

// Generate the MediaCodecsSupport enum, containing
// codec-specific SW/HW decode/encode information.
// Entries for HW audio decode/encode should never be set as we
// don't support HW audio decode/encode, but they are included
// for debug purposes / check for erroneous PDM return values.
// Example: MediaCodecsSupport::AACSoftwareDecode
enum class MediaCodecsSupport : int {
#define X(name)                                                       \
  SW_DECODE(name), HW_DECODE(name), SW_ENCODE(name), HW_ENCODE(name), \
      LACK_HW_EXTENSION(name),
  CODEC_LIST
#undef X
      SENTINEL
};

// Enumset containing per-codec SW/HW support
using MediaCodecsSupported = EnumSet<MediaCodecsSupport, uint64_t>;

// Whether the platform supports a DRM scheme. Unknown until it is queried.
enum class DrmSchemeSupport : uint8_t { Unknown, Unsupported, Supported };

// All decode related entries, including the lack of HW extension entries.
inline constexpr MediaCodecsSupported kMediaCodecsDecodeMask = {
#define X(name)                                                             \
  MediaCodecsSupport::SW_DECODE(name), MediaCodecsSupport::HW_DECODE(name), \
      MediaCodecsSupport::LACK_HW_EXTENSION(name),
    CODEC_LIST
#undef X
};

// All encode related entries.
inline constexpr MediaCodecsSupported kMediaCodecsEncodeMask = {
#define X(name) \
  MediaCodecsSupport::SW_ENCODE(name), MediaCodecsSupport::HW_ENCODE(name),
    CODEC_LIST
#undef X
};

#undef SW_DECODE
#undef HW_DECODE
#undef SW_ENCODE
#undef HW_ENCODE
#undef LACK_HW_EXTENSION
#undef CODEC_LIST  // end of macros!

// Codec-agnostic SW/HW decode support information.
enum class DecodeSupport : int {
  SoftwareDecode,
  HardwareDecode,
  UnsureDueToLackOfExtension,
};
using DecodeSupportSet = EnumSet<DecodeSupport, uint64_t>;

// Codec-agnostic SW/HW decode support information.
enum class EncodeSupport : int {
  SoftwareEncode,
  HardwareEncode,
  UnsureDueToLackOfExtension,
};
using EncodeSupportSet = EnumSet<EncodeSupport, uint64_t>;

// CodecDefinition stores information needed to convert / index
// codec support information between types. See: GetAllCodecDefinitions()
struct CodecDefinition {
  MediaCodec codec = MediaCodec::SENTINEL;
  const char* commonName = "Undefined codec name";
  const char* mimeTypeString = "Undefined MIME type string";
  MediaCodecsSupport swDecodeSupport = MediaCodecsSupport::SENTINEL;
  MediaCodecsSupport hwDecodeSupport = MediaCodecsSupport::SENTINEL;
  MediaCodecsSupport swEncodeSupport = MediaCodecsSupport::SENTINEL;
  MediaCodecsSupport hwEncodeSupport = MediaCodecsSupport::SENTINEL;
  MediaCodecsSupport lackOfHWExtenstion = MediaCodecsSupport::SENTINEL;
};

// Singleton class used to collect, manage, and report codec support data.
class MCSInfo final {
 public:
  // Gets the codec support from current PDMFactory and PEMFactory
  // configuration.
  static MediaCodecsSupported GetSupportFromFactory(bool aForceRefresh = false);

  // Add codec support information to our aggregated list of supported codecs.
  // Incoming support info is merged with the current support info.
  // This is because different PDMs may report different codec support
  // information, so merging their results allows us to maintain a
  // cumulative support list without overwriting any existing data.
  static void AddSupport(const MediaCodecsSupported& aSupport);

  // Return a cumulative list of codec support information.
  // Each call to AddSupport adds to or updates this list.
  // This support information can be used to create user-readable strings
  // to report codec support information in about:support.
  static MediaCodecsSupported GetSupport();

  // Reset codec support information saved from calls to AddSupport().
  static void ResetSupport();

  // Query a MediaCodecsSupported EnumSet for codec-specific SW/HW support enums
  // and return general support information as stored in a DecodeSupportSet.
  //
  // Example input:
  //
  //   aCodec: MediaCodec::H264
  //   aDecode: MediaCodecsSupport {
  //     MediaCodecsSupport::AACSoftwareDecode
  //     MediaCodecsSupport::H264HardwareDecode,
  //     MediaCodecsSupport::H264SoftwareDecode,
  //     MediaCodecsSupport::VP8SoftwareDecode,
  //   }
  //
  // Example output:
  //
  //   DecodeSupportSet {
  //     DecodeSupport::SoftwareDecode,
  //     DecodeSupport::HardwareDecode
  //   }
  //
  static DecodeSupportSet GetDecodeSupportSet(
      const MediaCodec& aCodec, const MediaCodecsSupported& aSupported);
  static EncodeSupportSet GetEncodeSupportSet(
      const MediaCodec& aCodec, const MediaCodecsSupported& aSupported);

  // Return codec-specific SW/HW support enums for a given codec.
  // The DecodeSupportSet argument is used which codec-specific SW/HW
  // support values are returned, if any.
  //
  // Example input:
  //   aCodec: MediaCodec::VP8
  //   aSupportSet: DecodeSupportSet {DecodeSupport::SoftwareDecode}
  //
  // Example output:
  //   MediaCodecsSupported {MediaCodecsSupport::VP8SoftwareDecode}
  //
  static MediaCodecsSupported GetDecodeMediaCodecsSupported(
      const MediaCodec& aCodec, const DecodeSupportSet& aSupportSet);
  static MediaCodecsSupported GetEncodeMediaCodecsSupported(
      const MediaCodec& aCodec, const EncodeSupportSet& aSupportSet);

  // Generate a plaintext description for the SW/HW support information
  // contained in a MediaCodecsSupported EnumSet.
  //
  // Example input:
  //   MediaCodecsSupported {
  //     MediaCodecsSupport::H264SoftwareDecode,
  //     MediaCodecsSupport::H264HardwareDecode,
  //     MediaCodecsSupport::VP8SoftwareDecode
  //   }
  //
  // Example output (returned via argument aCodecString)
  //
  //   "SW H264 decode\n
  //   HW H264 decode\n
  //   SW VP8 decode"_ns
  //
  static void GetMediaCodecsSupportedString(
      nsCString& aSupportString, const MediaCodecsSupported& aSupportedCodecs);

  // Returns a MediaCodec enum representing the given MIME type string.
  //
  // Example input:
  //   "audio/flac"_ns
  //
  // Example output:
  //   MediaCodec::FLAC
  //
  static MediaCodec GetMediaCodecFromMimeType(const nsACString& aMimeType);

  // Returns array of hardcoded codec definitions.
  static std::array<CodecDefinition, 13> GetAllCodecDefinitions();

  // Parses an array of MIME type strings and returns a MediaCodecSet.
  static MediaCodecSet GetMediaCodecSetFromMimeTypes(
      const nsTArray<nsCString>& aCodecStrings);

  // Returns a MediaCodecsSupport enum corresponding to the provided
  // codec type and decode support level requested.
  static MediaCodecsSupport GetMediaCodecsSupportEnum(
      const MediaCodec& aCodec, const DecodeSupport& aSupport);
  static MediaCodecsSupport GetMediaCodecsSupportEnum(
      const MediaCodec& aCodec, const EncodeSupport& aSupport);

  // Returns only the decode or encode entries present in the args.
  static MediaCodecsSupported GetDecodeSupported(
      const MediaCodecsSupported& aSupportedCodecs) {
    return aSupportedCodecs & kMediaCodecsDecodeMask;
  }
  static MediaCodecsSupported GetEncodeSupported(
      const MediaCodecsSupported& aSupportedCodecs) {
    return aSupportedCodecs & kMediaCodecsEncodeMask;
  }

  // Returns true if SW/HW decode enum for a given codec is present in the args.
  static bool SupportsSoftwareDecode(
      const MediaCodecsSupported& aSupportedCodecs, const MediaCodec& aCodec);
  static bool SupportsHardwareDecode(
      const MediaCodecsSupported& aSupportedCodecs, const MediaCodec& aCodec);

  // Returns true if SW/HW encode enum for a given codec is present in the args.
  static bool SupportsSoftwareEncode(
      const MediaCodecsSupported& aSupportedCodecs, const MediaCodec& aCodec);
  static bool SupportsHardwareEncode(
      const MediaCodecsSupported& aSupportedCodecs, const MediaCodec& aCodec);

#ifdef MOZ_WIDGET_ANDROID
  // Returns the subset of aSupport, the support reported for aCodec with 8-bit
  // content, which remains for aColorDepth. MediaCodec decoding is limited to
  // what the platform advertises, while software decoding for codecs other than
  // H264 and HEVC uses our own decoders and supports any color depth.
  static DecodeSupportSet GetMediaCodecDecodeSupportForColorDepth(
      const MediaCodec& aCodec, const DecodeSupportSet& aSupport,
      gfx::ColorDepth aColorDepth);

  // Returns aSupport, the support reported for aCodec, without hardware
  // decoding if aSize is larger than any hardware decoder supports.
  static DecodeSupportSet GetMediaCodecDecodeSupportForSize(
      const MediaCodec& aCodec, const DecodeSupportSet& aSupport,
      const gfx::IntSize& aSize);

  // Returns the maximum size to configure a hardware decoder for aCodec with,
  // for content which starts at aSize, so that it can change resolution
  // without reallocating its output buffers. The size has the same orientation
  // as aSize. Returns an empty size if the default decoder does not support
  // adaptive playback.
  static gfx::IntSize GetMediaCodecAdaptivePlaybackMaxSize(
      const MediaCodec& aCodec, const gfx::IntSize& aSize);
#endif

  MCSInfo(MCSInfo const&) = delete;
  void operator=(MCSInfo const&) = delete;
  ~MCSInfo() = default;

 private:
  MCSInfo();
  static MCSInfo* GetInstance(const StaticMutexAutoLock& /* unused */);

  // Returns a codec definition by MIME type name ("media/vp9")
  // or "common" name ("VP9")
  static CodecDefinition GetCodecDefinition(const MediaCodec& aCodec);

  UniquePtr<nsTHashMap<MediaCodecsSupport, CodecDefinition>> mHashTableMCS;
  UniquePtr<nsTHashMap<const char*, CodecDefinition>> mHashTableString;
  UniquePtr<nsTHashMap<MediaCodec, CodecDefinition>> mHashTableCodec;
  MediaCodecsSupported mSupport;
};
}  // namespace mozilla::media

namespace mozilla {
// Used for IPDL serialization.
// The 'value' has to be the biggest enum from MediaCodecsSupport.
template <typename T>
struct MaxEnumValue;
template <>
struct MaxEnumValue<media::MediaCodecsSupport> {
  static constexpr unsigned int value =
      static_cast<unsigned int>(media::MediaCodecsSupport::SENTINEL);
};
}  // namespace mozilla

#endif /* MediaCodecsSupport_h_ */
