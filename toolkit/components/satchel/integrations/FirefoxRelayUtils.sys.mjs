/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { LoginHelperDOM } from "moz-src:///toolkit/components/passwordmgr/shared/LoginHelperDOM.sys.mjs";

export const FirefoxRelayUtils = {
  isRelayInterestedField(input) {
    return (
      FirefoxRelayUtils.relayIsAvailableOrEnabled &&
      (LoginHelperDOM.isInferredEmailField(input) ||
        LoginHelperDOM.isInferredUsernameField(input))
    );
  },

  relayIsAvailableOrEnabled() {
    const value = Services.prefs.getStringPref(
      "signon.firefoxRelay.feature",
      undefined
    );
    return ["available", "offered", "enabled"].includes(value);
  },
};
