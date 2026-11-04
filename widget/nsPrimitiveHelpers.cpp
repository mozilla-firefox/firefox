/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//
// Part of the reason these routines are all in once place is so that as new
// data flavors are added that are known to be one-byte or two-byte strings, or
// even raw binary data, then we just have to go to one place to change how the
// data moves into/out of the primitives and native line endings.
//
// If you add new flavors that have special consideration (binary data or
// one-byte char* strings), please update all the helper classes in this file.
//
// For now, this is the assumption that we are making:
//  - text/plain is always a char*
//  - anything else is a char16_t*
//

#include "nsPrimitiveHelpers.h"

#include <memory>

#include "nsCOMPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsISupportsPrimitives.h"
#include "nsITransferable.h"
#include "nsLinebreakConverter.h"
#include "nsReadableUtils.h"
#include "nsXPCOM.h"

//
// CreatePrimitiveForData
//
// Given some data and the flavor it corresponds to, creates the appropriate
// nsISupports* wrapper for passing across IDL boundaries. Right now, everything
// creates a two-byte |nsISupportsString|, except for "text/rtf", native
// platform HTML (CF_HTML on win32) and custom clipboard data.
//
void nsPrimitiveHelpers ::CreatePrimitiveForData(const nsACString& aFlavor,
                                                 const void* aDataBuff,
                                                 size_t aDataLen,
                                                 nsISupports** aPrimitive) {
  if (!aPrimitive) {
    return;
  }

  if (aFlavor.EqualsLiteral(kNativeHTMLMime) ||
      aFlavor.EqualsLiteral(kRTFMime) ||
      aFlavor.EqualsLiteral(kCustomTypesMime) ||
      StringBeginsWith(aFlavor, nsLiteralCString(kWebCustomFormatPrefix))) {
    nsCOMPtr<nsISupportsCString> primitive =
        do_CreateInstance(NS_SUPPORTS_CSTRING_CONTRACTID);
    if (primitive) {
      const char* start = reinterpret_cast<const char*>(aDataBuff);
      primitive->SetData(Substring(start, start + aDataLen));
      NS_ADDREF(*aPrimitive = primitive);
    }
  } else {
    nsCOMPtr<nsISupportsString> primitive =
        do_CreateInstance(NS_SUPPORTS_STRING_CONTRACTID);
    if (primitive) {
      if (aDataLen % 2) {
        // Account for odd number of bytes.
        auto buffer = std::make_unique<char[]>(aDataLen + 1);
        memcpy(buffer.get(), aDataBuff, aDataLen);
        buffer[aDataLen] = 0;

        const char16_t* start = reinterpret_cast<const char16_t*>(buffer.get());
        // Recall that length takes length as characters, not bytes.
        primitive->SetData(Substring(start, start + ((aDataLen + 1) / 2)));
      } else {
        const char16_t* start = reinterpret_cast<const char16_t*>(aDataBuff);
        // Recall that length takes length as characters, not bytes.
        primitive->SetData(Substring(start, start + (aDataLen / 2)));
      }

      NS_ADDREF(*aPrimitive = primitive);
    }
  }

}  // CreatePrimitiveForData

//
// CreatePrimitiveForCFHTML
//
// Platform specific CreatePrimitive, windows CF_HTML.
//
void nsPrimitiveHelpers ::CreatePrimitiveForCFHTML(const void* aDataBuff,
                                                   uint32_t* aDataLen,
                                                   nsISupports** aPrimitive) {
  if (!aPrimitive) return;

  nsCOMPtr<nsISupportsString> primitive =
      do_CreateInstance(NS_SUPPORTS_STRING_CONTRACTID);
  if (!primitive) return;

  // We need to duplicate the input buffer, since the removal of linebreaks
  // might reallocte it.
  void* utf8 = moz_xmalloc(*aDataLen);
  memcpy(utf8, aDataBuff, *aDataLen);
  int32_t signedLen = static_cast<int32_t>(*aDataLen);
  nsLinebreakHelpers::ConvertPlatformToDOMLinebreaks(true, &utf8, &signedLen);
  *aDataLen = signedLen;

  nsAutoString str(
      NS_ConvertUTF8toUTF16(reinterpret_cast<const char*>(utf8), *aDataLen));
  free(utf8);
  *aDataLen = str.Length() * sizeof(char16_t);
  primitive->SetData(str);
  NS_ADDREF(*aPrimitive = primitive);
}

//
// CreateDataFromPrimitive
//
// Given a nsISupports* primitive and the flavor it represents, creates a new
// data buffer with the data in it. This data will be null terminated, but the
// length parameter does not reflect that.
//
void nsPrimitiveHelpers::CreateDataFromPrimitive(const nsACString& aFlavor,
                                                 nsISupports* aPrimitive,
                                                 void** aDataBuff,
                                                 uint32_t* aDataLen) {
  if (!aDataBuff) return;

  *aDataBuff = nullptr;
  *aDataLen = 0;

  if (aFlavor.EqualsLiteral(kCustomTypesMime) ||
      StringBeginsWith(aFlavor, nsLiteralCString(kWebCustomFormatPrefix))) {
    nsCOMPtr<nsISupportsCString> plainText(do_QueryInterface(aPrimitive));
    if (plainText) {
      nsAutoCString data;
      plainText->GetData(data);
      *aDataBuff = ToNewCString(data);
      *aDataLen = data.Length() * sizeof(char);
    }
  } else {
    nsCOMPtr<nsISupportsString> doubleByteText(do_QueryInterface(aPrimitive));
    if (doubleByteText) {
      nsAutoString data;
      doubleByteText->GetData(data);
      *aDataBuff = ToNewUnicode(data);
      *aDataLen = data.Length() * sizeof(char16_t);
    }
  }
}

//
// ConvertPlatformToDOMLinebreaks
//
// Given some data, convert from the platform linebreaks into the LF expected by
// the DOM. This will attempt to convert the data in place, but the buffer may
// still need to be reallocated regardless (disposing the old buffer is taken
// care of internally, see the note below).
//
// NOTE: this assumes that it can use 'free' to dispose of the old buffer.
//
nsresult nsLinebreakHelpers ::ConvertPlatformToDOMLinebreaks(
    bool aIsSingleByteChars, void** ioData, int32_t* ioLengthInBytes) {
  NS_ASSERTION(ioData && *ioData && ioLengthInBytes, "Bad Params");
  if (!(ioData && *ioData && ioLengthInBytes)) return NS_ERROR_INVALID_ARG;

  nsresult retVal = NS_OK;

  // RTF and CF_HTML on Windows are transfered as single-byte characters.
  if (aIsSingleByteChars) {
    char* buffAsChars = reinterpret_cast<char*>(*ioData);
    char* oldBuffer = buffAsChars;
    retVal = nsLinebreakConverter::ConvertLineBreaksInSitu(
        &buffAsChars, nsLinebreakConverter::eLinebreakAny,
        nsLinebreakConverter::eLinebreakContent, *ioLengthInBytes,
        ioLengthInBytes);
    if (NS_SUCCEEDED(retVal)) {
      if (buffAsChars != oldBuffer)  // check if buffer was reallocated
        free(oldBuffer);
      *ioData = buffAsChars;
    }
  } else {
    char16_t* buffAsUnichar = reinterpret_cast<char16_t*>(*ioData);
    char16_t* oldBuffer = buffAsUnichar;
    int32_t newLengthInChars;
    retVal = nsLinebreakConverter::ConvertUnicharLineBreaksInSitu(
        &buffAsUnichar, nsLinebreakConverter::eLinebreakAny,
        nsLinebreakConverter::eLinebreakContent,
        *ioLengthInBytes / sizeof(char16_t), &newLengthInChars);
    if (NS_SUCCEEDED(retVal)) {
      if (buffAsUnichar != oldBuffer)  // check if buffer was reallocated
        free(oldBuffer);
      *ioData = buffAsUnichar;
      *ioLengthInBytes = newLengthInChars * sizeof(char16_t);
    }
  }

  return retVal;

}  // ConvertPlatformToDOMLinebreaks
