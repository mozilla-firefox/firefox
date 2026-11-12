/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/TextDecoder.h"

#include <stdint.h>

#include "mozilla/Encoding.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/dom/BufferSourceBinding.h"
#include "mozilla/dom/UnionTypes.h"
#include "nsContentUtils.h"

namespace mozilla::dom {

void TextDecoder::Init(const nsAString& aLabel,
                       const TextDecoderOptions& aOptions, ErrorResult& aRv) {
  // Let encoding be the result of getting an encoding from label.
  // If encoding is failure or replacement, throw a RangeError
  // (https://encoding.spec.whatwg.org/#dom-textdecoder).
  const Encoding* encoding = Encoding::ForLabelNoReplacement(aLabel);
  if (!encoding) {
    NS_ConvertUTF16toUTF8 label(aLabel);
    label.Trim(" \t\n\f\r");
    aRv.ThrowRangeError<MSG_ENCODING_NOT_SUPPORTED>(label);
    return;
  }
  InitWithEncoding(WrapNotNull(encoding), aOptions);
}

void TextDecoder::InitWithEncoding(NotNull<const Encoding*> aEncoding,
                                   const TextDecoderOptions& aOptions) {
  aEncoding->Name(mEncoding);
  // Store the flags passed via our options dictionary.
  mFatal = aOptions.mFatal;
  mIgnoreBOM = aOptions.mIgnoreBOM;

  // Create a decoder object for mEncoding.
  if (mIgnoreBOM) {
    mDecoder = aEncoding->NewDecoderWithoutBOMHandling();
  } else {
    mDecoder = aEncoding->NewDecoderWithBOMRemoval();
  }
}

void TextDecoderCommon::DecodeNative(Span<const uint8_t> aInput,
                                     const bool aStream,
                                     nsAString& aOutDecodedString,
                                     ErrorResult& aRv) {
  aOutDecodedString.Truncate();

  auto resetState = mozilla::MakeScopeExit([&] {
    // This reuses existing memory, so it's OK to do this even
    // when we fail on OOM.

    // If the internal streaming flag of the decoder object is not set,
    // then reset the encoding algorithm state to the default values
    if (!aStream) {
      if (mIgnoreBOM) {
        mDecoder->Encoding()->NewDecoderWithoutBOMHandlingInto(*mDecoder);
      } else {
        mDecoder->Encoding()->NewDecoderWithBOMRemovalInto(*mDecoder);
      }
    }
  });

  CheckedInt<nsAString::size_type> needed =
      mDecoder->MaxUTF16BufferLength(aInput.Length());
  if (!needed.isValid()) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  auto output = aOutDecodedString.GetMutableData(needed.value(), fallible);
  if (!output) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  uint32_t result;
  size_t read;
  size_t written;
  if (mFatal) {
    std::tie(result, read, written) =
        mDecoder->DecodeToUTF16WithoutReplacement(aInput, *output, !aStream);
    if (result != kInputEmpty) {
      aRv.ThrowTypeError<MSG_DOM_DECODING_FAILED>();
      return;
    }
  } else {
    std::tie(result, read, written, std::ignore) =
        mDecoder->DecodeToUTF16(aInput, *output, !aStream);
  }
  MOZ_ASSERT(result == kInputEmpty);
  MOZ_ASSERT(read == aInput.Length());
  MOZ_ASSERT(written <= aOutDecodedString.Length());

  if (!aOutDecodedString.SetLength(written, fallible)) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }
}

void TextDecoder::Decode(const Optional<BufferSource>& aBuffer,
                         const TextDecodeOptions& aOptions,
                         nsAString& aOutDecodedString, ErrorResult& aRv) {
  if (!aBuffer.WasPassed()) {
    DecodeNative(nullptr, aOptions.mStream, aOutDecodedString, aRv);
    return;
  }

  ProcessTypedArrays(aBuffer.Value(), [&](const Span<uint8_t>& aData,
                                          JS::AutoCheckCannotGC&&) {
    DecodeNative(aData, aOptions.mStream, aOutDecodedString, aRv);
  });
}

void TextDecoderCommon::GetEncoding(nsAString& aEncoding) {
  CopyASCIItoUTF16(mEncoding, aEncoding);
  nsContentUtils::ASCIIToLower(aEncoding);
}

}  // namespace mozilla::dom
