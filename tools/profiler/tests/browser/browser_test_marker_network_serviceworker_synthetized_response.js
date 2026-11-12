/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Test that we emit network markers accordingly.
 * In this file we'll test a service worker that returns a synthetized response.
 * This means the service worker will make up a response by itself.
 */

const serviceWorkerFileName = "serviceworker_synthetized_response.js";
registerCleanupFunction(() => SpecialPowers.removeAllServiceWorkerData());

add_task(async function test_network_markers_service_worker_setup() {
  // Disabling cache makes the result more predictable. Also this makes things
  // simpler when dealing with service workers.
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.cache.disk.enable", false],
      ["browser.cache.memory.enable", false],
    ],
  });
});

add_task(async function test_network_markers_service_worker_register() {
  // In this first step, we request an HTML page that will register a service
  // worker. We'll wait until the service worker is fully installed before
  // checking various things.
  await ProfilerTestUtils.assertProfilerInactive();

  const url = `${BASE_URL_HTTPS}serviceworkers/serviceworker_register.html`;
  await BrowserTestUtils.withNewTab(url, async contentBrowser => {
    await SpecialPowers.spawn(
      contentBrowser,
      [serviceWorkerFileName],
      async function (serviceWorkerFileName) {
        await content.wrappedJSObject.registerServiceWorkerAndWait(
          serviceWorkerFileName
        );
      }
    );

    // Let's make sure we actually have a registered service workers.
    const workers = await SpecialPowers.registeredServiceWorkers();
    Assert.equal(
      workers.length,
      1,
      "One service worker should be properly registered."
    );
  });
});

add_task(async function test_network_markers_service_worker_use() {
  // In this test, we'll first load a plain html file, then do some fetch
  // requests in the context of the page. One request is served with a
  // synthetized response, the other request is served with a real "fetch" done
  // by the service worker.
  await ProfilerTestUtils.assertProfilerInactive();

  await ProfilerTestUtils.startProfilerForMarkerTests();

  const url = `${BASE_URL_HTTPS}serviceworkers/serviceworker_simple.html`;
  await BrowserTestUtils.withNewTab(url, async contentBrowser => {
    const contentPid = await SpecialPowers.spawn(
      contentBrowser,
      [],
      () => Services.appinfo.processID
    );

    await SpecialPowers.spawn(contentBrowser, [], async () => {
      // This request is served directly by the service worker as a synthetized response.
      await content
        .fetch("firefox-generated.svg")
        .then(res => res.arrayBuffer());

      // This request is served by a fetch done inside the service worker.
      await content
        .fetch("firefox-logo-nightly.svg")
        .then(res => res.arrayBuffer());
    });

    const { parentThread, contentThread, profile } =
      await stopProfilerNowAndGetThreads(contentPid);

    // The service worker work happens in a third "thread" or process, let's try
    // to find it.
    // Currently the fetches happen on the main thread for the content process,
    // this may change in the future and we may have to adapt this function.
    // Also please note this isn't necessarily the same content process as the
    // ones for the tab.
    const { serviceWorkerParentThread } = findServiceWorkerThreads(profile);

    ok(
      serviceWorkerParentThread,
      "We should find a thread for the service worker."
    );

    // By logging a few information about the threads we make debugging easier.
    logInformationForThread("parentThread information", parentThread);
    logInformationForThread("contentThread information", contentThread);
    logInformationForThread(
      "serviceWorkerParentThread information",
      serviceWorkerParentThread
    );

    const parentNetworkMarkers = ProfilerTestUtils.getInflatedNetworkMarkers(
      parentThread
    )
      // When we load a page, Firefox will check the service worker freshness
      // after a few seconds. So when the test lasts a long time (with some test
      // environments) we might see spurious markers about that that we're not
      // interesting in in this part of the test. They're only present in the
      // parent process.
      .filter(marker => !marker.data.URI.includes(serviceWorkerFileName));

    const contentNetworkMarkers =
      ProfilerTestUtils.getInflatedNetworkMarkers(contentThread);
    const serviceWorkerNetworkMarkers =
      ProfilerTestUtils.getInflatedNetworkMarkers(serviceWorkerParentThread);

    // Some more logs for debugging purposes.
    info(
      "Parent network markers: " + JSON.stringify(parentNetworkMarkers, null, 2)
    );
    info(
      "Content network markers: " +
        JSON.stringify(contentNetworkMarkers, null, 2)
    );
    info(
      "Serviceworker network markers: " +
        JSON.stringify(serviceWorkerNetworkMarkers, null, 2)
    );

    const parentPairs =
      ProfilerTestUtils.getPairsOfNetworkMarkers(parentNetworkMarkers);
    const contentPairs = ProfilerTestUtils.getPairsOfNetworkMarkers(
      contentNetworkMarkers
    );
    const serviceWorkerPairs = ProfilerTestUtils.getPairsOfNetworkMarkers(
      serviceWorkerNetworkMarkers
    );

    // First, make sure we properly matched all start with stop markers. This
    // means that both arrays should contain only arrays of 2 elements.
    parentPairs.forEach(pair =>
      Assert.equal(
        pair.length,
        2,
        `For the URL ${pair[0].data.URI} we should get 2 markers in the parent process.`
      )
    );

    contentPairs.forEach(pair =>
      Assert.equal(
        pair.length,
        2,
        `For the URL ${pair[0].data.URI} we should get 2 markers in the content process.`
      )
    );
    serviceWorkerPairs.forEach(pair =>
      Assert.equal(
        pair.length,
        2,
        `For the URL ${pair[0].data.URI} we should get 2 markers in the service worker process.`
      )
    );

    // Let's look at all pairs and make sure we requested all expected files.
    // In this test, we should have redirect markers as well as stop markers,
    // because this case generates internal redirects.
    // Let's create various arrays to help assert.

    let parentStopMarkers = parentPairs.map(([_, stopMarker]) => stopMarker);
    const contentStopMarkers = contentPairs.map(
      ([_, stopMarker]) => stopMarker
    );
    // In this test we have very different results in the various threads, so
    // we'll assert every case separately.
    // A simple function to help constructing better assertions:
    const fullUrl = filename => `${BASE_URL_HTTPS}serviceworkers/${filename}`;

    {
      // In the parent process, we have 8 network markers:
      // - twice the html file -- because it's not cached by the SW, we get the
      //   marker both for the initial request and for the request initied from the
      //   SW.
      // - twice the firefox svg file -- similar situation
      // - once the generated svg file -- this one isn't fetched by the SW but
      //   rather forged directly, so there's no "second fetch", and thus we have
      //   only one marker.
      // - for each of these files, we have first an internal redirect from the
      //   main channel to the service worker. => 3 redirect markers more.
      Assert.equal(
        parentStopMarkers.length,
        8, // 3 html files, 3 firefox svg files, 2 generated svg file
        "There should be 8 stop markers in the parent process."
      );

      // The "1" requests are the initial requests that are intercepted, coming
      // from the web page, while the "2" requests are requests to the network,
      // coming from the service worker. The 1 were requested before 2, 2 ends
      // before 1.
      // "Intercept" requests are the internal redirects from the main channel
      // to the service worker. They happen before others.
      const [
        htmlFetchIntercept,
        htmlFetch1,
        htmlFetch2,
        generatedSvgIntercept,
        generatedSvgFetch,
        firefoxSvgIntercept,
        firefoxSvgFetch1,
        firefoxSvgFetch2,
      ] = parentStopMarkers;

      /* ----- /HTML FILE ---- */
      Assert.objectContains(htmlFetchIntercept, {
        name: Expect.stringMatches(/Load \d+:.*serviceworker_simple.html/),
        data: Expect.objectContainsOnly({
          type: "Network",
          status: "STATUS_REDIRECT",
          URI: fullUrl("serviceworker_simple.html"),
          classOfService: "UrgentStart",
          requestStatus: "NS_OK",
          requestMethod: "GET",
          contentType: null,
          startTime: Expect.number(),
          endTime: Expect.number(),
          id: Expect.number(),
          pri: Expect.number(),
          priorityHeader: Expect.string(),
          redirectId: htmlFetch1.data.id,
          redirectType: "Internal",
          isHttpToHttpsRedirect: false,
          RedirectURI: fullUrl("serviceworker_simple.html"),
          cache: "Unresolved",
        }),
      });

      Assert.objectContains(htmlFetch1, {
        name: Expect.stringMatches(/Load \d+:.*serviceworker_simple.html/),
        data: Expect.objectContainsOnly({
          type: "Network",
          status: "STATUS_STOP",
          URI: fullUrl("serviceworker_simple.html"),
          httpVersion: "http/1.1",
          classOfService: "UrgentStart",
          requestStatus: "NS_OK",
          requestMethod: "GET",
          responseStatus: 200,
          contentType: "text/html",
          startTime: Expect.number(),
          endTime: Expect.number(),
          id: Expect.number(),
          pri: Expect.number(),
          priorityHeader: Expect.string(),
        }),
      });
      Assert.objectContains(htmlFetch2, {
        name: Expect.stringMatches(/Load \d+:.*serviceworker_simple.html/),
        data: Expect.objectContainsOnly({
          type: "Network",
          status: "STATUS_STOP",
          URI: fullUrl("serviceworker_simple.html"),
          httpVersion: "http/1.1",
          classOfService: "Unset",
          requestStatus: "NS_OK",
          requestMethod: "GET",
          responseStatus: 200,
          contentType: "text/html",
          // Because the request races with the cache, these 2 values are valid:
          // "Missed" when the cache answered before we get a result from the network.
          // "Unresolved" when we got a response from the network before the cache subsystem.
          cache: Expect.stringMatches(/^(Missed|Unresolved)$/),
          startTime: Expect.number(),
          endTime: Expect.number(),
          domainLookupStart: Expect.number(),
          domainLookupEnd: Expect.number(),
          connectStart: Expect.number(),
          secureConnectionStart: Expect.number(),
          tcpConnectEnd: Expect.number(),
          connectEnd: Expect.number(),
          requestStart: Expect.number(),
          responseStart: Expect.number(),
          responseEnd: Expect.number(),
          id: Expect.number(),
          count: Expect.number(),
          pri: Expect.number(),
          priorityHeader: Expect.string(),
        }),
      });
      /* ----- /HTML FILE ---- */

      /* ----- GENERATED SVG FILE ---- */
      Assert.objectContains(generatedSvgIntercept, {
        name: Expect.stringMatches(/Load \d+:.*firefox-generated.svg/),
        data: Expect.objectContainsOnly({
          type: "Network",
          status: "STATUS_REDIRECT",
          URI: fullUrl("firefox-generated.svg"),
          classOfService: "Unset",
          requestStatus: "NS_OK",
          requestMethod: "GET",
          contentType: null,
          startTime: Expect.number(),
          endTime: Expect.number(),
          id: Expect.number(),
          pri: Expect.number(),
          priorityHeader: Expect.string(),
          redirectId: generatedSvgFetch.data.id,
          redirectType: "Internal",
          isHttpToHttpsRedirect: false,
          RedirectURI: fullUrl("firefox-generated.svg"),
          cache: "Unresolved",
          innerWindowID: Expect.number(),
        }),
      });
      Assert.objectContains(generatedSvgFetch, {
        name: Expect.stringMatches(/Load \d+:.*firefox-generated.svg/),
        data: Expect.objectContainsOnly({
          type: "Network",
          status: "STATUS_STOP",
          URI: fullUrl("firefox-generated.svg"),
          httpVersion: "http/1.1",
          classOfService: "Unset",
          requestStatus: "NS_OK",
          requestMethod: "GET",
          responseStatus: 200,
          contentType: "image/svg+xml",
          startTime: Expect.number(),
          endTime: Expect.number(),
          id: Expect.number(),
          pri: Expect.number(),
          priorityHeader: Expect.string(),
          innerWindowID: Expect.number(),
        }),
      });
      /* ----- ∕GENERATED SVG FILE ---- */
      /* ----- REQUESTED SVG FILE ---- */
      Assert.objectContains(firefoxSvgIntercept, {
        name: Expect.stringMatches(/Load \d+:.*firefox-logo-nightly.svg/),
        data: Expect.objectContainsOnly({
          type: "Network",
          status: "STATUS_REDIRECT",
          URI: fullUrl("firefox-logo-nightly.svg"),
          classOfService: "Unset",
          requestStatus: "NS_OK",
          requestMethod: "GET",
          contentType: null,
          startTime: Expect.number(),
          endTime: Expect.number(),
          id: Expect.number(),
          pri: Expect.number(),
          priorityHeader: Expect.string(),
          redirectId: firefoxSvgFetch1.data.id,
          redirectType: "Internal",
          isHttpToHttpsRedirect: false,
          RedirectURI: fullUrl("firefox-logo-nightly.svg"),
          cache: "Unresolved",
          innerWindowID: Expect.number(),
        }),
      });
      Assert.objectContains(firefoxSvgFetch1, {
        name: Expect.stringMatches(/Load \d+:.*firefox-logo-nightly.svg/),
        data: Expect.objectContainsOnly({
          type: "Network",
          status: "STATUS_STOP",
          URI: fullUrl("firefox-logo-nightly.svg"),
          httpVersion: "http/1.1",
          classOfService: "Unset",
          requestStatus: "NS_OK",
          requestMethod: "GET",
          responseStatus: 200,
          contentType: "image/svg+xml",
          startTime: Expect.number(),
          endTime: Expect.number(),
          id: Expect.number(),
          pri: Expect.number(),
          priorityHeader: Expect.string(),
          innerWindowID: Expect.number(),
        }),
      });
      Assert.objectContains(firefoxSvgFetch2, {
        name: Expect.stringMatches(/Load \d+:.*firefox-logo-nightly.svg/),
        data: Expect.objectContainsOnly({
          type: "Network",
          status: "STATUS_STOP",
          URI: fullUrl("firefox-logo-nightly.svg"),
          httpVersion: "http/1.1",
          classOfService: "Unset",
          requestStatus: "NS_OK",
          requestMethod: "GET",
          responseStatus: 200,
          contentType: "image/svg+xml",
          // Because the request races with the cache, these 2 values are valid:
          // "Missed" when the cache answered before we get a result from the network.
          // "Unresolved" when we got a response from the network before the cache subsystem.
          cache: Expect.stringMatches(/^(Missed|Unresolved)$/),
          startTime: Expect.number(),
          endTime: Expect.number(),
          domainLookupStart: Expect.number(),
          domainLookupEnd: Expect.number(),
          connectStart: Expect.number(),
          secureConnectionStart: Expect.number(),
          tcpConnectEnd: Expect.number(),
          connectEnd: Expect.number(),
          requestStart: Expect.number(),
          responseStart: Expect.number(),
          responseEnd: Expect.number(),
          id: Expect.number(),
          count: Expect.number(),
          pri: Expect.number(),
          priorityHeader: Expect.string(),
          // Note: no innerWindowID here, is that a bug?
        }),
      });
      /* ----- ∕REQUESTED SVG FILE ---- */
    }

    // It's possible that the service worker thread IS the content thread, in
    // that case we'll get all markers in the same thread.
    // The "1" requests are the initial requests that are intercepted, coming
    // from the web page, while the "2" requests are the requests coming from
    // the service worker.
    let htmlFetch1, generatedSvgFetch1, firefoxSvgFetch1;

    // First, let's handle the case where the threads are different:
    if (serviceWorkerParentThread !== contentThread) {
      // In the content process (that is the process for the web page), we have
      // 3 network markers:
      // - 1 for the HTML page
      // - 1 for the generated svg file
      // - 1 for the firefox svg file
      // Indeed, the service worker interception is invisible from the context
      // of the web page, so we just get 3 "normal" requests. However these
      // requests will miss all timing information, because they're hidden by
      // the service worker interception. We may want to fix this...
      Assert.equal(
        contentStopMarkers.length,
        3, // 1 for each file
        "There should be 3 stop markers in the content process."
      );

      [htmlFetch1, generatedSvgFetch1, firefoxSvgFetch1] = contentStopMarkers;
    } else {
      // Else case: the service worker parent thread IS the content thread
      // (note: this is always the case with fission). In that case all network
      // markers tested in the above block are together in the same object.
      Assert.equal(
        contentStopMarkers.length,
        5,
        "There should be 5 stop markers in the combined process (containing both the content page and the service worker)"
      );

      // Because of how the test is done, these markers are ordered by the
      // position of the START markers.
      [
        // For the htmlFetch request, note that 2 is before 1, because that's
        // the top level navigation. Indeed for the top level navigation
        // everything happens first in the main process, possibly before a
        // content process even exists, and the content process is merely
        // notified at the end.
        htmlFetch1,
        generatedSvgFetch1,
        firefoxSvgFetch1,
      ] = contentStopMarkers;
    }

    // Let's test first the markers coming from the content page.
    Assert.objectContains(htmlFetch1, {
      name: Expect.stringMatches(/Load \d+:.*serviceworker_simple.html/),
      data: Expect.objectContainsOnly({
        type: "Network",
        status: "STATUS_STOP",
        URI: fullUrl("serviceworker_simple.html"),
        httpVersion: "http/1.1",
        classOfService: "Unset",
        requestStatus: "NS_OK",
        requestMethod: "GET",
        responseStatus: 200,
        contentType: "text/html",
        startTime: Expect.number(),
        endTime: Expect.number(),
        id: Expect.number(),
        pri: Expect.number(),
        priorityHeader: Expect.string(),
      }),
    });
    Assert.objectContains(generatedSvgFetch1, {
      name: Expect.stringMatches(/Load \d+:.*firefox-generated.svg/),
      data: Expect.objectContainsOnly({
        type: "Network",
        status: "STATUS_STOP",
        URI: fullUrl("firefox-generated.svg"),
        httpVersion: "http/1.1",
        classOfService: "Unset",
        requestStatus: "NS_OK",
        requestMethod: "GET",
        responseStatus: 200,
        contentType: "image/svg+xml",
        startTime: Expect.number(),
        endTime: Expect.number(),
        id: Expect.number(),
        pri: Expect.number(),
        priorityHeader: Expect.string(),
        innerWindowID: Expect.number(),
      }),
    });
    Assert.objectContains(firefoxSvgFetch1, {
      name: Expect.stringMatches(/Load \d+:.*firefox-logo-nightly.svg/),
      data: Expect.objectContainsOnly({
        type: "Network",
        status: "STATUS_STOP",
        URI: fullUrl("firefox-logo-nightly.svg"),
        httpVersion: "http/1.1",
        classOfService: "Unset",
        requestStatus: "NS_OK",
        requestMethod: "GET",
        responseStatus: 200,
        contentType: "image/svg+xml",
        startTime: Expect.number(),
        endTime: Expect.number(),
        id: Expect.number(),
        pri: Expect.number(),
        priorityHeader: Expect.string(),
        innerWindowID: Expect.number(),
      }),
    });
  });
});
