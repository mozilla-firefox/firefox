/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// On debug osx test machine, verify chaos mode takes slightly too long
requestLongerTimeout(2);

Services.prefs.setBoolPref("network.early-hints.enabled", true);

const { request_count_checking } = ChromeUtils.importESModule(
  "resource://testing-common/early_hint_preload_test_helper.sys.mjs"
);

// - testName is just there to be printed during Asserts when failing
// - asset is the asset type, see early_hint_asset_html.sjs for possible values
//   for the asset type fetch see test_hint_fetch due to timing issues
// - variant:
//   - "normal": no early hints, expects one normal request expected
//   - "hinted": early hints sent, expects one hinted request
//   - "reload": early hints sent, resources non-cacheable, two early-hint requests expected
//   - "cached": same as reload, but resources are cacheable, so only one hinted network request expected
async function test_hint_asset(testName, asset, variant) {
  // reset the count
  let headers = new Headers();
  headers.append("X-Early-Hint-Count-Start", "");
  await fetch(
    "http://example.com/browser/netwerk/test/browser/early_hint_pixel_count.sjs",
    { headers }
  );

  let requestUrl = `https://example.com/browser/netwerk/test/browser/early_hint_asset_html.sjs?as=${asset}&hinted=${
    variant !== "normal" ? "1" : "0"
  }&cached=${variant === "cached" ? "1" : "0"}`;

  let numConnectBackRemaining = 0;
  if (variant === "hinted") {
    numConnectBackRemaining = 1;
  } else if (variant === "reload" || variant === "cached") {
    numConnectBackRemaining = 2;
  }

  let observer = {
    QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),
    observe(aSubject, aTopic) {
      if (aTopic == "earlyhints-connectback") {
        numConnectBackRemaining -= 1;
      }
    },
  };
  Services.obs.addObserver(observer, "earlyhints-connectback");

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: requestUrl,
      waitForLoad: true,
    },
    async function (browser) {
      if (asset === "fetch") {
        // wait until the fetch is complete
        await TestUtils.waitForCondition(_ => {
          return SpecialPowers.spawn(browser, [], _ => {
            return (
              content.document.getElementsByTagName("h2")[0] != undefined &&
              content.document.getElementsByTagName("h2")[0].textContent !==
                "Fetching..." // default text set by early_hint_asset_html.sjs
            );
          });
        });
      }

      // reload
      if (variant === "reload" || variant === "cached") {
        await BrowserTestUtils.reloadTab(gBrowser.selectedTab);
      }

      if (asset === "fetch") {
        // wait until the fetch is complete
        await TestUtils.waitForCondition(_ => {
          return SpecialPowers.spawn(browser, [], _ => {
            return (
              content.document.getElementsByTagName("h2")[0] != undefined &&
              content.document.getElementsByTagName("h2")[0].textContent !==
                "Fetching..." // default text set by early_hint_asset_html.sjs
            );
          });
        });
      }
    }
  );
  Services.obs.removeObserver(observer, "earlyhints-connectback");

  let gotRequestCount = await fetch(
    "http://example.com/browser/netwerk/test/browser/early_hint_pixel_count.sjs"
  ).then(response => response.json());
  if (
    asset === "script" &&
    variant === "cached" &&
    numConnectBackRemaining === 1
  ) {
    // If the navigation cache is enabled
    // (dom.script_loader.experimental.navigation_cache),
    // the script can be cached in the per-process cache storage, and in that
    // case the channel isn't opened, and the "earlyhints-connectback"
    // notification isn't observed.
    info(
      `${testName} (${asset}+${variant}) in-memory-cached script's notification is skipped`
    );
  } else {
    Assert.equal(
      numConnectBackRemaining,
      0,
      `${testName} (${asset}+${variant}) no remaining connect back expected`
    );
  }

  let expectedRequestCount;
  if (variant === "normal") {
    expectedRequestCount = { hinted: 0, normal: 1 };
  } else if (variant === "hinted") {
    expectedRequestCount = { hinted: 1, normal: 0 };
  } else if (variant === "reload") {
    expectedRequestCount = { hinted: 2, normal: 0 };
  } else if (variant === "cached") {
    expectedRequestCount = { hinted: 1, normal: 0 };
  }

  await request_count_checking(
    `${testName} (${asset}+${variant})`,
    gotRequestCount,
    expectedRequestCount
  );
  if (variant === "cached") {
    Services.cache2.clear();
  }
}

// preload image
add_task(async function test_103_asset_image() {
  await test_hint_asset("test_103_asset_normal", "image", "normal");
  await test_hint_asset("test_103_asset_hinted", "image", "hinted");
  await test_hint_asset("test_103_asset_reload", "image", "reload");
  // TODO(Bug 1815884): await test_hint_asset("test_103_asset_cached", "image", "cached");
});

// preload css
add_task(async function test_103_asset_style() {
  await test_hint_asset("test_103_asset_normal", "style", "normal");
  await test_hint_asset("test_103_asset_hinted", "style", "hinted");
  await test_hint_asset("test_103_asset_reload", "style", "reload");
  // TODO(Bug 1815884): await test_hint_asset("test_103_asset_cached", "style", "cached");
});

// preload javascript
add_task(async function test_103_asset_javascript() {
  await test_hint_asset("test_103_asset_normal", "script", "normal");
  await test_hint_asset("test_103_asset_hinted", "script", "hinted");
  await test_hint_asset("test_103_asset_reload", "script", "reload");
  await test_hint_asset("test_103_asset_cached", "script", "cached");
});

// preload javascript module
add_task(async function test_103_asset_module() {
  await test_hint_asset("test_103_asset_normal", "module", "normal");
  await test_hint_asset("test_103_asset_hinted", "module", "hinted");
  await test_hint_asset("test_103_asset_reload", "module", "reload");
  await test_hint_asset("test_103_asset_cached", "module", "cached");
});

// preload font
add_task(async function test_103_asset_font() {
  await test_hint_asset("test_103_asset_normal", "font", "normal");
  await test_hint_asset("test_103_asset_hinted", "font", "hinted");
  await test_hint_asset("test_103_asset_reload", "font", "reload");
  await test_hint_asset("test_103_asset_cached", "font", "cached");
});

// preload fetch
add_task(async function test_103_asset_fetch() {
  await test_hint_asset("test_103_asset_normal", "fetch", "normal");
  await test_hint_asset("test_103_asset_hinted", "fetch", "hinted");
  await test_hint_asset("test_103_asset_reload", "fetch", "reload");
  await test_hint_asset("test_103_asset_cached", "fetch", "cached");
});
