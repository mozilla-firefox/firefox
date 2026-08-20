/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _include_ipc_glue_UtilityProcessSandboxing_h_
#define _include_ipc_glue_UtilityProcessSandboxing_h_

#include <stdint.h>

namespace mozilla {

namespace ipc {

// When adding a new value, the checks within UtilityProcessImpl::Init() needs
// to be updated as well.
enum SandboxingKind : uint64_t {

  GENERIC_UTILITY,

#ifdef MOZ_APPLEMEDIA
  UTILITY_AUDIO_DECODING_APPLE_MEDIA,
#endif
#ifdef XP_WIN
  UTILITY_AUDIO_DECODING_WMF,
#endif
#ifdef MOZ_WMF_MEDIA_ENGINE
  MF_MEDIA_ENGINE_CDM,
#endif
#ifdef XP_WIN
  WINDOWS_UTILS,
  WINDOWS_FILE_DIALOG,
#endif
#if defined(NIGHTLY_BUILD) && !defined(MOZ_NO_SMART_CARDS)
  PKCS11_MODULE,
#endif  // NIGHTLY_BUILD && !MOZ_NO_SMART_CARDS

#ifndef ANDROID
  HW_INFERENCE,
#endif  // !ANDROID

  // Add new kinds directly above this one: the gtests, and the
  // MOZ_DISABLE_CONTENT_SANDBOX "utility:N" syntax, refer to the pre-existing
  // ones by index.
  COUNT,
};

bool IsUtilitySandboxEnabled(const char* envVar, SandboxingKind aKind);
bool IsUtilitySandboxEnabled(SandboxingKind aKind);

}  // namespace ipc

}  // namespace mozilla

#endif  // _include_ipc_glue_UtilityProcessSandboxing_h_
