/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

loader.lazyGetter(this, "L10N", () => {
  return new Localization(["devtools/client/changes.ftl"], true);
});

module.exports = {
  getStr: id => L10N.formatValueSync(id),
  getAttributes: id => {
    const [{ attributes }] = L10N.formatMessagesSync([id]);
    return Object.fromEntries(
      attributes.map(({ name, value }) => [name, value])
    );
  },
};
