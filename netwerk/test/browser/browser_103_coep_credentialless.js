/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// An early hint preload is made before the document's
// Cross-Origin-Embedder-Policy is known, so it must not be used for a
// document whose policy would have changed the request.

"use strict";

const SJS_PATH =
  "/browser/netwerk/test/browser/early_hint_coep_credentialless.sjs";
const MAIN_URL = `https://example.com${SJS_PATH}`;
const SCRIPT_ORIGIN_URL = `https://test1.example.com${SJS_PATH}`;

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["network.early-hints.enabled", true],
      ["browser.tabs.remote.useCrossOriginEmbedderPolicy", true],
      ["browser.tabs.remote.coep.credentialless", true],
    ],
  });

  await BrowserTestUtils.withNewTab(
    `${SCRIPT_ORIGIN_URL}?mode=setcookie`,
    async () => {}
  );

  registerCleanupFunction(() => {
    Services.cookies.removeAll();
    Services.cache2.clear();
  });
});

async function loadPage(hinted, coep) {
  await fetch(`${MAIN_URL}?mode=reset`);

  let result;
  await BrowserTestUtils.withNewTab(
    `${MAIN_URL}?hinted=${hinted ? 1 : 0}&coep=${coep ? 1 : 0}`,
    async browser => {
      result = await SpecialPowers.spawn(
        browser,
        [],
        () => content.wrappedJSObject.scriptResult
      );
    }
  );

  let count = await fetch(`${MAIN_URL}?mode=count`).then(r => r.json());
  Services.cache2.clear();
  return { result, count };
}

add_task(async function test_preload_used_without_coep() {
  let { result, count } = await loadPage(true, false);
  Assert.equal(result, "COOKIE", "the script was loaded with the cookie");
  Assert.deepEqual(
    count,
    { hinted: 1, normal: 0 },
    "the preloaded script was used"
  );
});

add_task(async function test_coep_credentialless_without_preload() {
  let { result, count } = await loadPage(false, true);
  Assert.equal(result, "NO_COOKIE", "the script was loaded without cookies");
  Assert.deepEqual(count, { hinted: 0, normal: 1 });
});

add_task(async function test_coep_credentialless_with_preload() {
  let { result, count } = await loadPage(true, true);
  Assert.equal(
    result,
    "NO_COOKIE",
    "the credentialed preload wasn't used for the script"
  );
  Assert.deepEqual(
    count,
    { hinted: 1, normal: 1 },
    "the script was loaded again"
  );
});
