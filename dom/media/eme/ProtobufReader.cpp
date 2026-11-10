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

}  // namespace mozilla::widevine
