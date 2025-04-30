/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

console.warn(
  `When adding Zendesk support in Jira, Firefox calls the Storage Access API on behalf of the site. See https://bugzilla.mozilla.org/show_bug.cgi?id=1774592 for details.`
);

/**
 * The Jira Zendesk plugin requires storage access to the jiraplugin.zendesk.com
 * domain. This shim intercepts clicks on the Zendesk support button on the APP
 * marketplace page  and requests storage access for the domain.
 */

const STORAGE_ACCESS_ORIGIN = "https://jiraplugin.zendesk.com";

document.documentElement.addEventListener(
  "click",
  e => {
    const { target, isTrusted } = e;
    if (!isTrusted) {
      return;
    }
    const button = target.closest(
      "a[href*='/jira/marketplace/discover/app/zendesk_for_jira']"
    );
    if (!button) {
      return;
    }

    console.warn(
      "Calling the Storage Access API on behalf of " + STORAGE_ACCESS_ORIGIN
    );
    button.disabled = true;
    e.stopPropagation();
    e.preventDefault();
    document
      .requestStorageAccessForOrigin(STORAGE_ACCESS_ORIGIN)
      .then(() => {
        button.disabled = false;
        target.click();
      })
      .catch(() => {
        button.disabled = false;
      });
  },
  true
);
