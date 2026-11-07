/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { triggerSync } = ChromeUtils.importESModule(
  "resource://gre/modules/services-automation/ServicesAutomation.sys.mjs"
);

// Arguments is expected to be provided in the scope that this file is loaded
// into.
/* global arguments */

let resolve = arguments[3];
try {
  triggerSync(arguments[0], arguments[1], arguments[2]).then(res => {
    resolve(res);
  });
} catch (error) {
  let res = { logs: {}, result: 1, result_message: error.toString() };
  resolve(res);
}
