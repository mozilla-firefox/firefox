/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * Bug 1774567 - Zendesk Asana Support
 *
 * The Zendesk Asana support app requires storage access to the Asana Zendesk
 * integration domain. This shim intercepts clicks on the Zendesk tickets on the
 * Zendesk view page and requests storage access for the Asana integration
 * domain.
 */

console.warn(
  `When interacting with tickets on Zendesk, Firefox calls the Storage Access API on behalf of the site. See https://bugzilla.mozilla.org/show_bug.cgi?id=1949491 for details.`
);

const STORAGE_ACCESS_ORIGIN = "https://zendesk.integrations.asana.plus";

document.documentElement.addEventListener(
  "click",
  e => {
    const { target, isTrusted } = e;
    if (!isTrusted) {
      return;
    }

    // Find the click event on the ticket link.
    const button = target.closest("a[href*='tickets/']");
    if (!button) {
      return;
    }

    console.warn(
      "Calling the Storage Access API on behalf of " + STORAGE_ACCESS_ORIGIN
    );

    // Disable the button to prevent it from being clicked again.
    button.disabled = true;
    e.stopPropagation();
    e.preventDefault();
    document
      .requestStorageAccessForOrigin(STORAGE_ACCESS_ORIGIN)
      .finally(() => {
        // Re-enable the button and proceed.
        button.disabled = false;
        button.click();
      });
  },
  true
);
