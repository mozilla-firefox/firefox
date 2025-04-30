/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

let lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  ForgetAboutSite: "resource://gre/modules/ForgetAboutSite.sys.mjs",
});

window.addEventListener("load", () => {
  let retVals = window.arguments[0];

  document.addEventListener("dialogaccept", e => {
    e.preventDefault();
    lazy.ForgetAboutSite.removeDataFromBaseDomain(retVals.host).catch(
      console.error
    );
    window.close();
  });

  document.addEventListener("dialogcancel", e => {
    e.preventDefault();
    window.close();
  });

  document.l10n.setAttributes(
    document.getElementById("clear-data-for-site-list"),
    "clear-data-for-site-list",
    {
      site: retVals.hostOrBaseDomain,
    }
  );
});
