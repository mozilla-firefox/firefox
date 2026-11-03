/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SHELL_WINDOWS_RUST_SRC_LAF_SERVICE_H_
#define SHELL_WINDOWS_RUST_SRC_LAF_SERVICE_H_

#include "ErrorList.h"
#include "nsID.h"
#include "mozilla/dom/Promise.h"
#include "nsStringFwd.h"

extern "C" {
nsresult new_limited_access_feature_service(REFNSIID iid, void** result);
nsresult shell_windows_new_secondary_tile_service(REFNSIID iid, void** result);
nsresult shell_windows_taskbar_can_pin_to_taskbar();
nsresult shell_windows_taskbar_is_current_app_pinned(
    const nsAString* aumid, const mozilla::dom::Promise* promise);
nsresult shell_windows_taskbar_pin_app_to_taskbar(
    const nsAString* aumid, const nsAString* shortcut_path,
    bool fire_and_forget, bool prompt_only,
    const mozilla::dom::Promise* promise);
nsresult shell_windows_taskbar_unpin_shortcut_from_taskbar(
    const nsAString* shortcut_path);
};

#endif  // SHELL_WINDOWS_RUST_SRC_LAF_SERVICE_H_
