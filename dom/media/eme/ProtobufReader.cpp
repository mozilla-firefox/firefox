/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ProtobufReader.h"

namespace mozilla::widevine {

Span<const uint8_t> TrimKeyRequest(Span<const uint8_t> aRequest) {
  ProtobufReader reader(aRequest);
  bool hasType = false;
  while (!reader.AtEnd()) {
    const size_t start = reader.Position();
    uint32_t field;
    Span<const uint8_t> payload;
    if (!reader.ReadField(field, payload)) {
      return aRequest;
    }

    if (field != kSignedMessageTypeField) {
      continue;
    }

    if (hasType) {
      return aRequest.To(start);
    }
    hasType = true;
  }
  return aRequest;
}

bool IsServiceCertificateRequest(Span<const uint8_t> aRequest) {
  return aRequest.Length() == kSignedMessageTypeOnlySize &&
         aRequest[0] == kSignedMessageTypeTag &&
         aRequest[1] ==
             static_cast<uint8_t>(SignedMessageType::ServiceCertificateRequest);
}

bool IsServiceCertificateResponse(Span<const uint8_t> aResponse) {
  return aResponse.Length() >= kSignedMessageTypeOnlySize &&
         aResponse[0] == kSignedMessageTypeTag &&
         aResponse[1] ==
             static_cast<uint8_t>(SignedMessageType::ServiceCertificate);
}

nsTArray<nsTArray<uint8_t>> ParseLicenseKeyIds(Span<const uint8_t> aResponse) {
  nsTArray<nsTArray<uint8_t>> keyIds;
  uint32_t field;
  Span<const uint8_t> payload;

  ProtobufReader message(aResponse);
  while (!message.AtEnd() && message.ReadField(field, payload)) {
    if (field != kSignedMessageMsgField) {
      continue;
    }

    ProtobufReader license(payload);
    while (!license.AtEnd() && license.ReadField(field, payload)) {
      if (field != kLicenseKeyField) {
        continue;
      }

      ProtobufReader container(payload);
      Span<const uint8_t> keyId;
      uint64_t type = 0;
      uint64_t value;
      while (!container.AtEnd() && container.ReadField(field, payload, value)) {
        if (field == kKeyContainerIdField && payload.Length() == kKeyIdSize) {
          keyId = payload;
        } else if (field == kKeyContainerTypeField) {
          type = value;
        }
      }

      if (keyId.IsEmpty() || type != kKeyTypeContent) {
        continue;
      }
      if (keyIds.Length() >= kMaxLicenseKeyIds) {
        return keyIds;
      }
      keyIds.AppendElement(nsTArray<uint8_t>(keyId.Elements(), kKeyIdSize));
    }
  }
  return keyIds;
}

}  // namespace mozilla::widevine
