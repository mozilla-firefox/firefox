/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* exported setupRegime */

async function setupRegime() {
  const { AddonManager } = ChromeUtils.importESModule(
    "resource://gre/modules/AddonManager.sys.mjs"
  );
  const addon = await AddonManager.getAddonByID("firefox-alpenglow@mozilla.org");
  await addon.enable();
  const root = window.document.documentElement;
  for (let waited = 0; !root.hasAttribute("lwtheme"); waited += 100) {
    if (waited >= 5000) {
      throw new Error(
        `regime did not take: no lwtheme after 5 s (add-on active: ${addon.isActive})`
      );
    }
    await new Promise(resolve => window.setTimeout(resolve, 100));
  }
  await new Promise(resolve => window.requestAnimationFrame(resolve));
  return { active: addon.isActive, lwtheme: true };
}
