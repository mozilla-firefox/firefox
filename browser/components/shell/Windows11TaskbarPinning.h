/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This file exists to keep the Windows 11 Taskbar Pinning API
 * related code as self-contained as possible.
 */

#ifndef SHELL_WINDOWS11TASKBARPINNING_H_
#define SHELL_WINDOWS11TASKBARPINNING_H_

#include "nsString.h"
#include <wrl.h>
#include <windows.h>  // for HRESULT

enum class Win11PinToTaskBarResultStatus {
  Failed,
  NotCurrentlyAllowed,
  AlreadyPinned,
  NotPinned,
  Success,
  NotSupported,
};

struct Win11PinToTaskBarResult {
  HRESULT errorCode;
  Win11PinToTaskBarResultStatus result;
};

Win11PinToTaskBarResult PinCurrentAppToTaskbarWin11(
    bool aCheckOnly, const nsAString& aAppUserModelId,
    const bool aFireAndForget);

Win11PinToTaskBarResult IsCurrentAppPinnedToTaskbarWin11();

#endif  // SHELL_WINDOWS11TASKBARPINNING_H_
