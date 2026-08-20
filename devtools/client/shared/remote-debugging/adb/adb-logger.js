/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * Logger shared by all the ADB modules. Enable via eg MOZ_LOG=devtools_adb:5
 */
exports.logger = console.createInstance({
  prefix: "devtools_adb",
  maxLogLevel: "Warn",
});
