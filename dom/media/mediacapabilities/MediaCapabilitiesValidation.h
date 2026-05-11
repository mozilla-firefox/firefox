/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_MEDIACAPABILITIES_MEDIACAPABILITIESVALIDATION_H_
#define DOM_MEDIA_MEDIACAPABILITIES_MEDIACAPABILITIESVALIDATION_H_
#include "mozilla/DefineEnum.h"
#include "mozilla/Result.h"
#include "mozilla/Variant.h"
#include "nsStringFwd.h"
// Media Capabilities API spec validation check functions.
// These functions are used as a first pass to filter out non-compliant
// configurations according to the spec. Lower-level SW/HW compability checks
// are handled in MediaCapabilities.cpp.
//
// See also:
// https://github.com/w3c/media-capabilities
// commit: b9d2f30c00c534ac44179f546bc60f421ce78070
namespace mozilla {
class ErrorResult;
class MediaExtendedMIMEType;
template <typename T>
class Maybe;

namespace dom {
class Promise;
struct MediaDecodingConfiguration;
struct MediaEncodingConfiguration;
enum class MediaEncodingType : uint8_t;
enum class MediaDecodingType : uint8_t;
enum class ColorGamut : uint8_t;
enum class TransferFunction : uint8_t;
}  // namespace dom

namespace mediacaps {
enum class AVType { AUDIO, VIDEO };
MOZ_DEFINE_ENUM_CLASS_WITH_TOSTRING(
    ValidationError,
    (MissingType, InvalidAudioConfiguration, InvalidVideoConfiguration,
     InvalidMIMEType, InvalidAudioType, InvalidVideoType, SingleCodecHasParams,
     ContainerMissingCodecsParam, ContainerCodecsNotSingle, FramerateInvalid,
     InapplicableMember, KeySystemWrongType, KeySystemAudioMissing,
     KeySystemVideoMissing, SENTINEL));

// Variant to consolidate encoding/decoding checks into the same functions
using MediaType = Variant<dom::MediaEncodingType, dom::MediaDecodingType>;
using ValidationResult = mozilla::Result<mozilla::Ok, ValidationError>;

struct BehaviorConfig;

ValidationResult CheckMIMETypeSupport(
    const MediaExtendedMIMEType& aMime,
    const MediaType& aEncodingOrDecodingType,
    const Maybe<dom::ColorGamut>& aColorGamut,
    const Maybe<dom::TransferFunction>& aTransferFunction,
    const BehaviorConfig& aBehavior);

// https://w3c.github.io/media-capabilities/#mediaconfiguration
ValidationResult IsValidMediaDecodingConfiguration(
    const dom::MediaDecodingConfiguration& aConfig,
    const BehaviorConfig& aBehavior);

// NOTE: No formal validation steps in the spec.
//       Currently just calls IsValidMediaConfiguration.
ValidationResult IsValidMediaEncodingConfiguration(
    const dom::MediaEncodingConfiguration& aConfig,
    const BehaviorConfig& aBehavior);

// Helpers which also convert the validation error to text
void RejectWithValidationResult(dom::Promise* aPromise,
                                const ValidationError aErr);
void ThrowWithValidationResult(ErrorResult& aRv, const ValidationError aErr);

// Used in MediaCapabilities.cpp WebRTC checks
bool IsMediaTypeWebRTC(const MediaType& aType);
}  // namespace mediacaps
}  // namespace mozilla

#endif
