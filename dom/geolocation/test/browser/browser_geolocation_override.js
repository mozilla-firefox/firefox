const GEO_URL =
  "http://mochi.test:8888/tests/dom/geolocation/test/mochitest/network_geolocation.sjs";

const PAGE_URL =
  "https://example.com/browser/dom/tests/browser/file_empty.html";

const required_preferences = [
  ["geo.provider.network.url", GEO_URL],
  ["geo.timeout", 100],
];

add_task(async function test_getCurrentPosition() {
  await SpecialPowers.pushPrefEnv({
    set: required_preferences,
  });

  let pageLoaded;
  let browser;
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    () => {
      gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser, PAGE_URL);
      browser = gBrowser.selectedBrowser;
      pageLoaded = BrowserTestUtils.browserLoaded(browser, true);
    },
    false
  );
  await pageLoaded;

  await SpecialPowers.spawn(browser, [], async () => {
    await SpecialPowers.pushPermissions([
      {
        type: "geo",
        allow: SpecialPowers.Services.perms.ALLOW_ACTION,
        context: content.document,
      },
    ]);

    info("Check original geolocation");
    const positionPromise = new Promise(resolve =>
      content.window.navigator.geolocation.getCurrentPosition(position => {
        resolve(position.coords.toJSON());
      })
    );
    const coordinates = await positionPromise;
    is(coordinates.latitude, 37.41857, "Original latitude is returned");
    is(coordinates.longitude, -122.08769, "Original longitude is returned");
    is(coordinates.accuracy, 42, "Original accuracy is returned");

    info("Override the geolocation");
    const browsingContext = content.browsingContext;
    browsingContext.setGeolocationServiceOverride({
      coords: {
        latitude: 10,
        longitude: 10,
        accuracy: 5,
        altitude: NaN,
        altitudeAccuracy: NaN,
        heading: NaN,
        speed: NaN,
      },
      timestamp: Date.now(),
    });
    const positionPromise2 = new Promise(resolve =>
      content.window.navigator.geolocation.getCurrentPosition(position => {
        resolve(position.coords.toJSON());
      })
    );
    const coordinates2 = await positionPromise2;
    is(coordinates2.latitude, 10, "Overridden latitude is returned");
    is(coordinates2.longitude, 10, "Overridden longitude is returned");
    is(coordinates2.accuracy, 5, "Overridden accuracy is returned");

    info("Override the geolocation again");
    browsingContext.setGeolocationServiceOverride({
      coords: {
        latitude: 20,
        longitude: 20,
        accuracy: 10,
        altitude: 10,
        altitudeAccuracy: 5,
        heading: 10,
        speed: 7,
      },
      timestamp: Date.now(),
    });
    const positionPromise3 = new Promise(resolve =>
      content.window.navigator.geolocation.getCurrentPosition(position => {
        resolve(position.coords.toJSON());
      })
    );
    const coordinates3 = await positionPromise3;
    is(coordinates3.latitude, 20, "Overridden latitude is returned");
    is(coordinates3.longitude, 20, "Overridden longitude is returned");
    is(coordinates3.accuracy, 10, "Overridden accuracy is returned");
    is(coordinates3.altitude, 10, "Overridden altitude is returned");
    is(
      coordinates3.altitudeAccuracy,
      5,
      "Overridden altitudeAccuracy is returned"
    );
    is(coordinates3.heading, 10, "Overridden heading is returned");
    is(coordinates3.speed, 7, "Overridden speed is returned");

    info("Reset the geolocation override");
    browsingContext.setGeolocationServiceOverride();
    const positionPromise4 = new Promise(resolve =>
      content.window.navigator.geolocation.getCurrentPosition(position => {
        resolve(position.coords.toJSON());
      })
    );
    const coordinates4 = await positionPromise4;
    is(coordinates4.latitude, 37.41857, "Original latitude is returned");
    is(coordinates4.longitude, -122.08769, "Original longitude is returned");
    is(coordinates4.accuracy, 42, "Original accuracy is returned");
  });

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_watchPosition() {
  await SpecialPowers.pushPrefEnv({
    set: required_preferences,
  });

  let pageLoaded;
  let browser;
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    () => {
      gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser, PAGE_URL);
      browser = gBrowser.selectedBrowser;
      pageLoaded = BrowserTestUtils.browserLoaded(browser, true);
    },
    false
  );
  await pageLoaded;

  await SpecialPowers.spawn(browser, [], async () => {
    await SpecialPowers.pushPermissions([
      {
        type: "geo",
        allow: SpecialPowers.Services.perms.ALLOW_ACTION,
        context: content.document,
      },
    ]);

    const browsingContext = content.browsingContext;

    // Set the initial override before the watchPosition is started.
    browsingContext.setGeolocationServiceOverride({
      coords: {
        latitude: 0,
        longitude: 0,
        accuracy: 0,
        altitude: NaN,
        altitudeAccuracy: NaN,
        heading: NaN,
        speed: NaN,
      },
      timestamp: Date.now(),
    });

    const watchID = content.window.navigator.geolocation.watchPosition(
      result => {
        const event = new content.window.CustomEvent("watchPosition", {
          detail: result.coords.toJSON(),
        });

        content.document.dispatchEvent(event);
      }
    );

    info("Override the geolocation");

    const onWatchPosition = new Promise(resolve => {
      content.document.addEventListener(
        "watchPosition",
        e => {
          resolve(e.detail);
        },
        { once: true }
      );
    });

    browsingContext.setGeolocationServiceOverride({
      coords: {
        latitude: 10,
        longitude: 10,
        accuracy: 5,
        altitude: NaN,
        altitudeAccuracy: NaN,
        heading: NaN,
        speed: NaN,
      },
      timestamp: Date.now(),
    });

    const result = await onWatchPosition;

    is(result.latitude, 10, "Overridden latitude is returned");
    is(result.longitude, 10, "Overridden longitude is returned");
    is(result.accuracy, 5, "Overridden accuracy is returned");

    info("Override the geolocation again");
    const onWatchPosition2 = new Promise(resolve => {
      content.document.addEventListener(
        "watchPosition",
        e => {
          resolve(e.detail);
        },
        { once: true }
      );
    });

    browsingContext.setGeolocationServiceOverride({
      coords: {
        latitude: 20,
        longitude: 20,
        accuracy: 10,
        altitude: 10,
        altitudeAccuracy: 5,
        heading: 10,
        speed: 7,
      },
      timestamp: Date.now(),
    });

    const result2 = await onWatchPosition2;

    is(result2.latitude, 20, "Overridden latitude is returned");
    is(result2.longitude, 20, "Overridden longitude is returned");
    is(result2.accuracy, 10, "Overridden accuracy is returned");
    is(result2.altitude, 10, "Overridden altitude is returned");
    is(result2.altitudeAccuracy, 5, "Overridden altitudeAccuracy is returned");
    is(result2.heading, 10, "Overridden heading is returned");
    is(result2.speed, 7, "Overridden speed is returned");

    content.window.navigator.geolocation.clearWatch(watchID);
  });

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_tab_reload() {
  await SpecialPowers.pushPrefEnv({
    set: required_preferences,
  });

  let pageLoaded;
  let browser;
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    () => {
      gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser, PAGE_URL);
      browser = gBrowser.selectedBrowser;
      pageLoaded = BrowserTestUtils.browserLoaded(browser, true);
    },
    false
  );
  await pageLoaded;

  await SpecialPowers.spawn(browser, [], async () => {
    await SpecialPowers.pushPermissions([
      {
        type: "geo",
        allow: SpecialPowers.Services.perms.ALLOW_ACTION,
        context: content.document,
      },
    ]);

    info("Check original geolocation");
    const positionPromise = new Promise(resolve =>
      content.window.navigator.geolocation.getCurrentPosition(position => {
        resolve(position.coords.toJSON());
      })
    );
    const coordinates = await positionPromise;
    is(coordinates.latitude, 37.41857, "Original latitude is returned");
    is(coordinates.longitude, -122.08769, "Original longitude is returned");
    is(coordinates.accuracy, 42, "Original accuracy is returned");

    info("Override the geolocation");
    const browsingContext = content.browsingContext;
    browsingContext.setGeolocationServiceOverride({
      coords: {
        latitude: 10,
        longitude: 10,
        accuracy: 5,
        altitude: NaN,
        altitudeAccuracy: NaN,
        heading: NaN,
        speed: NaN,
      },
      timestamp: Date.now(),
    });
    const positionPromise2 = new Promise(resolve =>
      content.window.navigator.geolocation.getCurrentPosition(position => {
        resolve(position.coords.toJSON());
      })
    );
    const coordinates2 = await positionPromise2;
    is(coordinates2.latitude, 10, "Overridden latitude is returned");
    is(coordinates2.longitude, 10, "Overridden longitude is returned");
    is(coordinates2.accuracy, 5, "Overridden accuracy is returned");
  });

  await BrowserTestUtils.reloadTab(tab);

  await SpecialPowers.spawn(browser, [], async () => {
    const browsingContext = content.browsingContext;

    info("Override the geolocation again");
    browsingContext.setGeolocationServiceOverride({
      coords: {
        latitude: 20,
        longitude: 20,
        accuracy: 10,
        altitude: 10,
        altitudeAccuracy: 5,
        heading: 10,
        speed: 7,
      },
      timestamp: Date.now(),
    });
    const positionPromise3 = new Promise(resolve =>
      content.window.navigator.geolocation.getCurrentPosition(position => {
        resolve(position.coords.toJSON());
      })
    );
    const coordinates3 = await positionPromise3;
    is(coordinates3.latitude, 20, "Overridden latitude is returned");
    is(coordinates3.longitude, 20, "Overridden longitude is returned");
    is(coordinates3.accuracy, 10, "Overridden accuracy is returned");
    is(coordinates3.altitude, 10, "Overridden altitude is returned");
    is(
      coordinates3.altitudeAccuracy,
      5,
      "Overridden altitudeAccuracy is returned"
    );
    is(coordinates3.heading, 10, "Overridden heading is returned");
    is(coordinates3.speed, 7, "Overridden speed is returned");

    info("Reset the geolocation override");
    browsingContext.setGeolocationServiceOverride();
    const positionPromise4 = new Promise(resolve =>
      content.window.navigator.geolocation.getCurrentPosition(position => {
        resolve(position.coords.toJSON());
      })
    );
    const coordinates4 = await positionPromise4;
    is(coordinates4.latitude, 37.41857, "Original latitude is returned");
    is(coordinates4.longitude, -122.08769, "Original longitude is returned");
    is(coordinates4.accuracy, 42, "Original accuracy is returned");
  });

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_amount_of_updates_for_watchPosition() {
  await SpecialPowers.pushPrefEnv({
    set: required_preferences,
  });

  let pageLoaded;
  let browser;
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    () => {
      gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser, PAGE_URL);
      browser = gBrowser.selectedBrowser;
      pageLoaded = BrowserTestUtils.browserLoaded(browser, true);
    },
    false
  );
  await pageLoaded;

  await SpecialPowers.spawn(browser, [], async () => {
    await SpecialPowers.pushPermissions([
      {
        type: "geo",
        allow: SpecialPowers.Services.perms.ALLOW_ACTION,
        context: content.document,
      },
    ]);

    const browsingContext = content.browsingContext;

    // Set the initial override before the watchPosition is started.
    browsingContext.setGeolocationServiceOverride({
      coords: {
        latitude: 0,
        longitude: 0,
        accuracy: 0,
        altitude: NaN,
        altitudeAccuracy: NaN,
        heading: NaN,
        speed: NaN,
      },
      timestamp: Date.now(),
    });

    const watchID = content.window.navigator.geolocation.watchPosition(
      result => {
        const event = new content.window.CustomEvent("watchPosition", {
          detail: result.coords.toJSON(),
        });

        content.document.dispatchEvent(event);
      }
    );
    const events = [];

    info("Override the geolocation");

    content.document.addEventListener("watchPosition", e => {
      content.window.console.log("test");
      events.push(e.detail);
    });

    browsingContext.setGeolocationServiceOverride({
      coords: {
        latitude: 10,
        longitude: 10,
        accuracy: 5,
        altitude: NaN,
        altitudeAccuracy: NaN,
        heading: NaN,
        speed: NaN,
      },
      timestamp: Date.now(),
    });

    await ContentTaskUtils.waitForCondition(() => !!events.length);

    is(events.length, 1, "Only one event should come after override is set");

    content.window.navigator.geolocation.clearWatch(watchID);
  });

  BrowserTestUtils.removeTab(tab);
});
