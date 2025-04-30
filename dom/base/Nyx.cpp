/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Nyx.h"
#include "mozilla/dom/TypedArray.h"

namespace mozilla::dom {

/* static */
void Nyx::Log(const GlobalObject&, const nsACString& aMsg) {
  MOZ_FUZZING_NYX_PRINTF("%s\n", PromiseFlatCString(aMsg).get());
}

/* static */
bool Nyx::IsEnabled(const GlobalObject&, const nsACString& aFuzzerName) {
  return fuzzing::Nyx::instance().is_enabled(
      PromiseFlatCString(aFuzzerName).get());
}

/* static */
bool Nyx::IsReplay(const GlobalObject&) {
#ifdef FUZZING_SNAPSHOT
  return fuzzing::Nyx::instance().is_replay();
#endif
  return false;
}

/* static */
bool Nyx::IsStarted(const GlobalObject&) {
  return fuzzing::Nyx::instance().started();
}

/* static */
void Nyx::Start(const GlobalObject&) {
  MOZ_FUZZING_NYX_PRINT("INFO: Performing snapshot...\n");
  fuzzing::Nyx::instance().start();
}

/* static */
void Nyx::Release(const GlobalObject&, uint32_t aIterations) {
  MOZ_FUZZING_NYX_PRINT("INFO: Reverting snapshot...\n");
  fuzzing::Nyx::instance().release(aIterations);
}

/* static */
void Nyx::GetRawData(const GlobalObject& aGlobal,
                     JS::MutableHandle<JSObject*> aRetval, ErrorResult& aRv) {
  uint8_t* buf = nullptr;
  uint32_t size = fuzzing::Nyx::instance().get_raw_data(&buf);
  if (buf == nullptr) {
    MOZ_FUZZING_NYX_PRINT("ERROR: Failed to get pointer to global payload.\n");
  }

  auto* cx = aGlobal.Context();
  JS::Rooted<JSObject*> arrayBuffer(
      cx, JS::NewArrayBufferWithUserOwnedContents(cx, size, buf));

  if (!arrayBuffer) {
    MOZ_FUZZING_NYX_PRINT("ERROR: Failed to create ArrayBuffer.\n");
    return;
  }

  aRetval.set(arrayBuffer);
}

}  // namespace mozilla::dom
