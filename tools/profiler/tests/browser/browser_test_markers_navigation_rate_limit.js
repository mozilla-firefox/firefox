/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Test that a profiler marker is emitted for each history.replaceState call
 * that gets ignored because of the navigation rate limit.
 */
add_task(async function test_navigation_rate_limit_markers() {
  const limit = 5;
  const timespanSeconds = 10;
  const callCount = 8;

  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.navigation.navigationRateLimit.count", limit],
      ["dom.navigation.navigationRateLimit.timespan", timespanSeconds],
    ],
  });

  info("Start the profiler to capture markers.");
  await ProfilerTestUtils.startProfilerForMarkerTests();

  const url = BASE_URL + "single_frame.html";
  await BrowserTestUtils.withNewTab(url, async contentBrowser => {
    const { contentPid, innerWindowId } = await SpecialPowers.spawn(
      contentBrowser,
      [],
      () => ({
        contentPid: Services.appinfo.processID,
        innerWindowId: content.windowGlobalChild.innerWindowId,
      })
    );

    await SpecialPowers.spawn(contentBrowser, [callCount], count => {
      // Run the calls from a page script so they aren't treated as system
      // callers, which are exempt from the rate limit.
      const script = content.document.createElement("script");
      script.textContent = `
        for (let i = 0; i < ${count}; i++) {
          history.replaceState(null, "", "#" + i);
        }
      `;
      content.document.body.append(script);
    });

    const { contentThread } = await stopProfilerNowAndGetThreads(contentPid);

    const markers = ProfilerTestUtils.getInflatedMarkerData(
      contentThread
    ).filter(m => m.data?.type === "NavigationRateLimited");

    Assert.equal(
      markers.length,
      callCount - limit,
      "There is one marker for each call over the limit"
    );

    for (const marker of markers) {
      Assert.equal(marker.name, "Navigation rate limited");
      Assert.equal(marker.data.limit, limit);
      Assert.equal(marker.data.timespan, timespanSeconds * 1000);
      Assert.equal(marker.data.innerWindowID, innerWindowId);
      Assert.equal(typeof marker.data.stack, "object", "Has a stack");
    }
  });
});
