/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

"use strict";

const TEST_URI =
  // eslint-disable-next-line @microsoft/sdl/no-insecure-url
  "http://example.com/browser/dom/geolocation/test/browser/geo_leak_test.html";

const BASE_GEO_URL =
  "http://mochi.test:8888/tests/dom/geolocation/test/mochitest/network_geolocation.sjs";

add_task(async function () {
  Services.prefs.setBoolPref("geo.prompt.testing", true);
  Services.prefs.setBoolPref("geo.prompt.testing.allow", true);

  // Make the geolocation provider responder very slowly to ensure that
  // it does not reply before we close the tab.
  Services.prefs.setCharPref(
    "geo.provider.network.url",
    BASE_GEO_URL + "?delay=100000"
  );

  // Open the test URI and close it. The test harness will make sure that the
  // page is cleaned up after some GCs. If geolocation is not shut down properly,
  // it will show up as a non-shutdown leak.
  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: TEST_URI,
    },
    function () {
      /* ... */
    }
  );

  ok(true, "Need to do something in this test");
});
