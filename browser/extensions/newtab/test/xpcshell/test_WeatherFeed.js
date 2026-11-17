/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  actionCreators: "resource://newtab/common/Actions.mjs",
  actionTypes: "resource://newtab/common/Actions.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  GeolocationTestUtils:
    "resource://testing-common/GeolocationTestUtils.sys.mjs",
  MerinoTestUtils: "resource://testing-common/MerinoTestUtils.sys.mjs",
  WeatherFeed: "resource://newtab/lib/WeatherFeed.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
});

const { WEATHER_SUGGESTION } = MerinoTestUtils;
GeolocationTestUtils.init(this);
MerinoTestUtils.init(this);

const WEATHER_ENABLED = "browser.newtabpage.activity-stream.showWeather";
const SYS_WEATHER_ENABLED =
  "browser.newtabpage.activity-stream.system.showWeather";

add_task(async function test_MerinoClient_wrapper_passes_correct_args() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(WeatherFeed.prototype, "PersistentCache").returns({
    set: () => {},
    get: () => {},
  });

  let feed = new WeatherFeed();
  let client = feed.MerinoClient("TEST_CLIENT");

  Assert.equal(
    typeof client.name,
    "string",
    "MerinoClient name should be a string, not an object"
  );
  Assert.equal(
    client.name,
    "TEST_CLIENT",
    "MerinoClient name should match the passed argument"
  );

  sandbox.restore();
});

add_task(async function test_construction() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(WeatherFeed.prototype, "PersistentCache").returns({
    set: () => {},
    get: () => {},
  });

  let feed = new WeatherFeed();

  info("WeatherFeed constructor should create initial values");

  Assert.ok(feed, "Could construct a WeatherFeed");
  Assert.strictEqual(feed.loaded, false, "WeatherFeed is not loaded");
  Assert.strictEqual(feed.merino, null, "merino is initialized as null");
  Assert.strictEqual(
    feed.suggestions.length,
    0,
    "suggestions is initialized as a array with length of 0"
  );
  Assert.strictEqual(
    feed.fetchTimer,
    null,
    "fetchTimer is initialized as null"
  );
  sandbox.restore();
});

add_task(async function test_checkOptInRegion() {
  let sandbox = sinon.createSandbox();

  sandbox.stub(WeatherFeed.prototype, "PersistentCache").returns({
    set: () => {},
    get: () => {},
  });

  let feed = new WeatherFeed();

  feed.store = {
    dispatch: sinon.spy(),
    getState() {
      return { Prefs: { values: {} } };
    },
  };

  sandbox.stub(feed, "isEnabled").returns(true);

  // First case: If home region is in the opt-in list, showWeatherOptIn should be true
  // Region._setHomeRegion() is the supported way to control region in tests:
  // https://firefox-source-docs.mozilla.org/toolkit/modules/toolkit_modules/Region.html#testing
  // We used false here because that second argument is a change observer that will fire an event.
  // So keeping it false silently sets the region for our test
  Region._setHomeRegion("FR", false);
  let resultTrue = await feed.checkOptInRegion();

  Assert.strictEqual(
    resultTrue,
    true,
    "Returns true for region in opt-in list"
  );
  Assert.ok(
    feed.store.dispatch.calledWith(
      actionCreators.SetPref("system.showWeatherOptIn", true)
    ),
    "Dispatch sets system.showWeatherOptIn to true when region is in opt-in list"
  );

  // Second case: If home region is not in the opt-in list, showWeatherOptIn should be false
  Region._setHomeRegion("ZZ", false);
  let resultFalse = await feed.checkOptInRegion();

  Assert.strictEqual(
    resultFalse,
    false,
    "Returns false for region not found in opt-in list"
  );
  Assert.ok(
    feed.store.dispatch.calledWith(
      actionCreators.SetPref("system.showWeatherOptIn", false)
    ),
    "Dispatch sets system.showWeatherOptIn to false when region is not in opt-in list"
  );

  sandbox.restore();
});

add_task(async function test_onAction_INIT() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(WeatherFeed.prototype, "MerinoClient").returns({
    get: () => [WEATHER_SUGGESTION],
    on: () => {},
  });
  sandbox.stub(WeatherFeed.prototype, "PersistentCache").returns({
    set: () => {},
    get: () => {},
  });
  const dateNowTestValue = 1;
  sandbox.stub(WeatherFeed.prototype, "Date").returns({
    now: () => dateNowTestValue,
  });

  let feed = new WeatherFeed();
  let locationData = {
    city: "testcity",
    adminArea: "",
    country: "",
  };

  Services.prefs.setBoolPref(WEATHER_ENABLED, true);
  Services.prefs.setBoolPref(SYS_WEATHER_ENABLED, true);

  sandbox.stub(feed, "isEnabled").returns(true);

  sandbox.stub(feed, "_fetchHelper").resolves({
    suggestions: [WEATHER_SUGGESTION],
    hourlyForecasts: [],
  });
  feed.locationData = locationData;
  feed.store = {
    dispatch: sinon.spy(),
    getState() {
      return this.state;
    },
    state: {
      Prefs: {
        values: {
          "weather.query": "348794",
        },
      },
    },
  };

  info("WeatherFeed.onAction INIT should initialize Weather");

  await feed.onAction({
    type: actionTypes.INIT,
  });

  Assert.equal(feed.store.dispatch.callCount, 2);
  Assert.ok(
    feed.store.dispatch.calledWith(
      actionCreators.BroadcastToContent({
        type: actionTypes.WEATHER_UPDATE,
        data: {
          suggestions: [WEATHER_SUGGESTION],
          hourlyForecasts: [],
          lastUpdated: dateNowTestValue,
          locationData,
        },
      })
    )
  );
  Services.prefs.clearUserPref(WEATHER_ENABLED);
  sandbox.restore();
});

// Test if location lookup was successful
add_task(async function test_onAction_opt_in_location_success() {
  let sandbox = sinon.createSandbox();

  sandbox.stub(WeatherFeed.prototype, "PersistentCache").returns({
    set: () => {},
    get: () => {},
  });

  let feed = new WeatherFeed();

  feed.store = {
    dispatch: sinon.spy(),
    getState() {
      return { Prefs: { values: {} } };
    },
  };

  // Stub _fetchNormalizedLocation() to simulate a successful lookup
  sandbox.stub(feed, "_fetchNormalizedLocation").resolves({
    localized_name: "Testville",
    administrative_area: "Paris",
    country: "FR",
    key: "12345",
  });

  await feed.onAction({ type: actionTypes.WEATHER_USER_OPT_IN_LOCATION });

  Assert.ok(
    feed.store.dispatch.calledWith(
      actionCreators.SetPref("weather.optInAccepted", true)
    )
  );
  Assert.ok(
    feed.store.dispatch.calledWith(
      actionCreators.SetPref("weather.optInDisplayed", false)
    )
  );

  // Assert location data broadcasted to content
  Assert.ok(
    feed.store.dispatch.calledWith(
      actionCreators.BroadcastToContent({
        type: actionTypes.WEATHER_LOCATION_DATA_UPDATE,
        data: {
          city: "Testville",
          adminName: "Paris",
          country: "FR",
        },
      })
    ),
    "Broadcasts WEATHER_LOCATION_DATA_UPDATE with normalized location data"
  );

  Assert.ok(
    feed.store.dispatch.calledWith(
      actionCreators.SetPref("weather.query", "12345")
    ),
    "Sets weather.query pref from location key"
  );

  sandbox.restore();
});

// Test if no location was found
add_task(async function test_onAction_opt_in_no_location_found() {
  let sandbox = sinon.createSandbox();

  sandbox.stub(WeatherFeed.prototype, "PersistentCache").returns({
    set: () => {},
    get: () => {},
  });

  let feed = new WeatherFeed();

  feed.store = {
    dispatch: sinon.spy(),
    getState() {
      return { Prefs: { values: {} } };
    },
  };

  // Test that _fetchNormalizedLocation doesn't return a location
  sandbox.stub(feed, "_fetchNormalizedLocation").resolves(null);

  await feed.onAction({ type: actionTypes.WEATHER_USER_OPT_IN_LOCATION });

  // Ensure the pref flips always happens so user won’t see the opt-in again
  Assert.ok(
    feed.store.dispatch.calledWith(
      actionCreators.SetPref("weather.optInAccepted", true)
    )
  );
  Assert.ok(
    feed.store.dispatch.calledWith(
      actionCreators.SetPref("weather.optInDisplayed", false)
    )
  );

  Assert.ok(
    !feed.store.dispatch.calledWithMatch(
      actionCreators.BroadcastToContent({
        type: actionTypes.WEATHER_LOCATION_DATA_UPDATE,
      })
    ),
    "Doesn't broadcast location data if location not found"
  );

  Assert.ok(
    !feed.store.dispatch.calledWith(
      actionCreators.SetPref("weather.query", sinon.match.any)
    ),
    "Does not set weather.query if no detected location"
  );

  sandbox.restore();
});

add_task(async function test_fetchLocationAutocomplete_broadcasts_results() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(WeatherFeed.prototype, "PersistentCache").returns({
    set: () => {},
    get: () => {},
  });

  let feed = new WeatherFeed();
  feed.store = {
    dispatch: sinon.spy(),
    getState() {
      return { Weather: { locationSearchString: "Tor" } };
    },
  };
  let autoComplete = sandbox.stub();
  feed.merino = { autoCompleteWeatherLocation: autoComplete };

  const toronto = { key: "55488", localized_name: "Toronto" };
  const cases = [
    { response: { locations: [toronto] }, expected: [toronto] },
    { response: { locations: [] }, expected: [] },
    { response: null, expected: [] },
  ];
  for (const { response, expected } of cases) {
    feed.store.dispatch.resetHistory();
    autoComplete.resolves(response);

    await feed.fetchLocationAutocomplete();

    Assert.deepEqual(
      feed.store.dispatch.firstCall.args[0],
      actionCreators.BroadcastToContent({
        type: actionTypes.WEATHER_LOCATION_SUGGESTIONS_UPDATE,
        data: expected,
      }),
      `Broadcasts ${expected.length} suggestion(s) for ${JSON.stringify(response)}`
    );
  }

  sandbox.restore();
});

// Test fetching weather information using GeolocationUtils.geolocation()
add_task(async function test_fetch_weather_with_geolocation() {
  const TEST_DATA = [
    {
      geolocation: {
        country_code: "US",
        region_code: "CA",
        region: "Califolnia",
        city: "San Francisco",
      },
      expected: {
        country: "US",
        region: "CA",
        city: "San Francisco",
      },
    },
    {
      geolocation: {
        country_code: "JP",
        region_code: "14",
        region: "Kanagawa",
        city: "",
      },
      expected: {
        country: "JP",
        region: "14",
        city: "Kanagawa",
      },
    },
    {
      geolocation: {
        country_code: "TestCountry",
        region_code: "",
        region: "TestRegion",
        city: "TestCity",
      },
      expected: {
        country: "TestCountry",
        region: "TestRegion",
        city: "TestCity",
      },
    },
    {
      // Test city-state fallback: Singapore (no region field)
      geolocation: {
        country_code: "SG",
        region_code: null,
        region: null,
        city: "Singapore",
      },
      expected: {
        country: "SG",
        region: "Singapore", // City used as fallback for region
        city: "Singapore",
      },
    },
    {
      // Test city-state fallback: Monaco (no region field)
      geolocation: {
        country_code: "MC",
        city: "Monaco",
      },
      expected: {
        country: "MC",
        region: "Monaco", // City used as fallback for region
        city: "Monaco",
      },
    },
    {
      geolocation: {
        country_code: "TestCountry",
      },
      expected: false,
    },
    {
      geolocation: {
        region_code: "TestRegionCode",
      },
      expected: false,
    },
    {
      geolocation: {
        region: "TestRegion",
      },
      expected: false,
    },
    {
      geolocation: {
        city: "TestCity",
      },
      expected: false,
    },
    {
      geolocation: {},
      expected: false,
    },
    {
      geolocation: null,
      expected: false,
    },
  ];

  for (let { geolocation, expected } of TEST_DATA) {
    info(`Test for ${JSON.stringify(geolocation)}`);

    let sandbox = sinon.createSandbox();
    sandbox.stub(WeatherFeed.prototype, "PersistentCache").returns({
      set: () => {},
      get: () => {},
    });

    let feed = new WeatherFeed();
    sandbox.stub(feed, "isEnabled").returns(true);
    feed.store = {
      dispatch: sinon.spy(),
      getState() {
        return { Prefs: { values: {} } };
      },
    };
    feed.merino = feed.MerinoClient();

    // Stub merino client
    let stub = sandbox.stub(feed.merino, "fetchWeatherReport").resolves(null);
    let cleanupGeolocationStub =
      GeolocationTestUtils.stubGeolocation(geolocation);

    await feed.onAction({ type: actionTypes.SYSTEM_TICK });

    if (expected) {
      sinon.assert.calledOnce(stub);
      sinon.assert.calledWith(stub, {
        source: "newtab",
        locationName: undefined,
        ...expected,
        timeoutMs: 7000,
      });
    } else {
      sinon.assert.notCalled(stub);
    }

    await cleanupGeolocationStub();
    sandbox.restore();
  }
});

// Bug 2046143: the Nova widget shows the opt-in prompt in place of weather, so
// the feed must not geolocate + fetch location weather before the user opts in.
// The gate is scoped to nova.enabled so the legacy widget (which renders its
// opt-in dialog only over a fetched suggestion) keeps fetching as before.
add_task(async function test_fetch_weather_optin_gate() {
  const VALID_GEOLOCATION = {
    country_code: "DE",
    region_code: "BE",
    region: "Berlin",
    city: "Berlin",
  };

  const TEST_DATA = [
    {
      desc: "nova + region opt-in required, not accepted -> no fetch",
      values: {
        "nova.enabled": true,
        "system.showWeatherOptIn": true,
        "weather.optInAccepted": false,
      },
      shouldFetch: false,
    },
    {
      desc: "nova + trainhop opt-in required, not accepted -> no fetch",
      values: {
        "nova.enabled": true,
        trainhopConfig: { weather: { weatherOptInEnabled: true } },
        "weather.optInAccepted": false,
      },
      shouldFetch: false,
    },
    {
      desc: "nova + widgetWeather trainhop opt-in required, not accepted -> no fetch",
      values: {
        "nova.enabled": true,
        trainhopConfig: { widgetWeather: { weatherOptInEnabled: true } },
        "weather.optInAccepted": false,
      },
      shouldFetch: false,
    },
    {
      desc: "nova + widgetWeather opt-in false overrides legacy weather key -> fetch",
      values: {
        "nova.enabled": true,
        trainhopConfig: {
          widgetWeather: { weatherOptInEnabled: false },
          weather: { weatherOptInEnabled: true },
        },
        "weather.optInAccepted": false,
      },
      shouldFetch: true,
    },
    {
      desc: "nova + opt-in required, accepted -> fetch",
      values: {
        "nova.enabled": true,
        "system.showWeatherOptIn": true,
        "weather.optInAccepted": true,
      },
      shouldFetch: true,
    },
    {
      desc: "nova + non-opt-in region -> fetch",
      values: {
        "nova.enabled": true,
        "system.showWeatherOptIn": false,
        "weather.optInAccepted": false,
      },
      shouldFetch: true,
    },
    {
      desc: "legacy (nova disabled) + opt-in required, not accepted -> still fetch",
      values: {
        "nova.enabled": false,
        "system.showWeatherOptIn": true,
        "weather.optInAccepted": false,
      },
      shouldFetch: true,
    },
  ];

  for (let { desc, values, shouldFetch } of TEST_DATA) {
    info(desc);

    let sandbox = sinon.createSandbox();
    sandbox.stub(WeatherFeed.prototype, "PersistentCache").returns({
      set: () => {},
      get: () => {},
    });

    let feed = new WeatherFeed();
    sandbox.stub(feed, "isEnabled").returns(true);
    feed.store = {
      dispatch: sinon.spy(),
      getState() {
        return { Prefs: { values } };
      },
    };
    feed.merino = feed.MerinoClient();

    let stub = sandbox.stub(feed.merino, "fetchWeatherReport").resolves(null);
    let cleanupGeolocationStub =
      GeolocationTestUtils.stubGeolocation(VALID_GEOLOCATION);

    await feed.onAction({ type: actionTypes.SYSTEM_TICK });

    if (shouldFetch) {
      sinon.assert.calledOnce(stub);
    } else {
      sinon.assert.notCalled(stub);
    }

    await cleanupGeolocationStub();
    sandbox.restore();
  }
});

// Test detecting location using GeolocationUtils.geolocation()
add_task(async function test_detect_location_with_geolocation() {
  const TEST_DATA = [
    {
      geolocation: {
        city: "San Francisco",
      },
      expected: "San Francisco",
    },
    {
      geolocation: {
        city: "",
        region: "Yokohama",
      },
      expected: "Yokohama",
    },
    {
      geolocation: {
        region: "Tokyo",
      },
      expected: "Tokyo",
    },
    {
      geolocation: {
        city: "",
        region: "",
      },
      expected: false,
    },
    {
      geolocation: {},
      expected: false,
    },
    {
      geolocation: null,
      expected: false,
    },
  ];
  for (let { geolocation, expected } of TEST_DATA) {
    info(`Test for ${JSON.stringify(geolocation)}`);

    let sandbox = sinon.createSandbox();
    sandbox.stub(WeatherFeed.prototype, "PersistentCache").returns({
      set: () => {},
      get: () => {},
    });

    let feed = new WeatherFeed();
    feed.store = {
      dispatch: sinon.spy(),
      getState() {
        return { Prefs: { values: {} } };
      },
    };
    feed.merino = { fetch: () => {} };

    // Stub merino client
    let stub = sandbox.stub(feed.merino, "fetch").resolves(null);
    // Stub geolocation
    let cleanupGeolocationStub =
      GeolocationTestUtils.stubGeolocation(geolocation);
    await feed.onAction({ type: actionTypes.WEATHER_USER_OPT_IN_LOCATION });

    if (expected) {
      sinon.assert.calledOnce(stub);
      sinon.assert.calledWith(stub, {
        otherParams: { request_type: "location", source: "newtab" },
        providers: ["accuweather"],
        query: expected,
        timeoutMs: 7000,
      });
    } else {
      sinon.assert.notCalled(stub);
    }

    await cleanupGeolocationStub();
    sandbox.restore();
  }
});

// Creates a WeatherFeed with stubbed merino methods and a virtual setTimeout
// so that _fetchHelper retry behavior can be driven synchronously.

function setupFetchHelperHarness(sandbox, outcomes, hourlyOutcomes = null) {
  // Prevent the "next fetch" scheduling inside fetchHelper().
  sandbox.stub(WeatherFeed.prototype, "restartFetchTimer").returns(undefined);

  // Stub setTimeout to capture the retry callback without actually waiting.
  // triggerRetry() fires it on demand so the test controls timing exactly.
  let timeoutCallback = null;
  const setTimeoutStub = sandbox
    .stub(WeatherFeed.prototype, "setTimeout")
    .callsFake(cb => {
      timeoutCallback = cb;
      return 1;
    });

  const feed = new WeatherFeed();

  // When testing hourly retries, enable the forecast widget so _fetchHelper
  // calls fetchHourlyForecasts inside its Promise.all.
  // weather.display and widgets.system.weatherForecast.enabled are the two
  // flags _fetchHelper checks to decide whether to call fetchHourlyForecasts.
  const prefValues =
    hourlyOutcomes !== null
      ? {
          "weather.display": "detailed",
          "widgets.system.weatherForecast.enabled": true,
          "widgets.weather.size": "large",
        }
      : {};

  feed.store = {
    dispatch: sinon.spy(),
    getState() {
      return { Prefs: { values: prefValues } };
    },
  };

  const fetchStub = sinon.stub();
  outcomes.forEach((outcome, index) => {
    if (outcome === "reject") {
      fetchStub.onCall(index).rejects(new Error(`fail${index}`));
    } else if (outcome === "resolve") {
      fetchStub.onCall(index).resolves({ city_name: "RetryCity" });
    }
  });

  feed.merino = { fetchWeatherReport: fetchStub };

  if (hourlyOutcomes !== null) {
    const fetchHourlyStub = sinon.stub();
    hourlyOutcomes.forEach((outcome, index) => {
      if (outcome === "reject") {
        fetchHourlyStub.onCall(index).rejects(new Error(`hourlyFail${index}`));
      } else if (outcome === "resolve") {
        fetchHourlyStub.onCall(index).resolves([{ hour: 0 }]);
      }
    });
    feed.merino.fetchHourlyForecasts = fetchHourlyStub;
  }

  return {
    feed,
    setTimeoutStub,
    triggerRetry: () => timeoutCallback && timeoutCallback(),
  };
}

add_task(async function test_fetchHelper_retry_resolve() {
  const sandbox = sinon.createSandbox();

  const { feed, setTimeoutStub, triggerRetry } = setupFetchHelperHarness(
    sandbox,
    ["reject", "resolve"]
  );

  // After retry success, fetchHelper should resolve to RetryCity.
  const promise = feed._fetchHelper(1, "q");

  // Two microtask turns are needed: one for Promise.all to process the
  // rejection, and one for the catch block to run and call setTimeout.
  await Promise.resolve();
  await Promise.resolve();

  Assert.equal(feed.merino.fetchWeatherReport.callCount, 1);
  Assert.equal(setTimeoutStub.callCount, 1);
  Assert.ok(
    setTimeoutStub.calledWith(sinon.match.func, 60 * 1000),
    "retry waits 60s (virtually)"
  );

  // Fire the retry.
  triggerRetry();
  const results = await promise;

  Assert.equal(
    feed.merino.fetchWeatherReport.callCount,
    2,
    "retried exactly once"
  );
  Assert.deepEqual(
    results,
    { suggestions: [{ city_name: "RetryCity" }], hourlyForecasts: [] },
    "returned retry result"
  );

  sandbox.restore();
});

add_task(async function test_fetchHelper_retry_reject() {
  const sandbox = sinon.createSandbox();

  const { feed, setTimeoutStub, triggerRetry } = setupFetchHelperHarness(
    sandbox,
    ["reject", "reject"]
  );

  // After retry also fails, fetchHelper should resolve to [].
  const promise = feed._fetchHelper(1, "q");

  // Two microtask turns are needed: one for Promise.all to process the
  // rejection, and one for the catch block to run and call setTimeout.
  await Promise.resolve();
  await Promise.resolve();

  Assert.equal(feed.merino.fetchWeatherReport.callCount, 1);
  Assert.equal(setTimeoutStub.callCount, 1);
  Assert.ok(
    setTimeoutStub.calledWith(sinon.match.func, 60 * 1000),
    "retry waits 60s (virtually)"
  );

  // Fire the retry.
  triggerRetry();
  const results = await promise;

  Assert.equal(
    feed.merino.fetchWeatherReport.callCount,
    2,
    "retried exactly once then gave up"
  );
  Assert.deepEqual(
    results,
    { suggestions: [], hourlyForecasts: [] },
    "returns empty object after exhausting retries"
  );

  sandbox.restore();
});

add_task(async function test_fetchHelper_hourly_failure_nonfatal() {
  const sandbox = sinon.createSandbox();

  // Hourly rejects, but report succeeds — result should still include the
  // weather report and no retry should be scheduled.
  const { feed, setTimeoutStub } = setupFetchHelperHarness(
    sandbox,
    ["resolve"],
    ["reject"]
  );

  const results = await feed._fetchHelper(1, "q");

  Assert.equal(feed.merino.fetchWeatherReport.callCount, 1);
  Assert.equal(feed.merino.fetchHourlyForecasts.callCount, 1);
  Assert.equal(setTimeoutStub.callCount, 0, "no retry scheduled");
  Assert.deepEqual(
    results,
    { suggestions: [{ city_name: "RetryCity" }], hourlyForecasts: [] },
    "report returned even when hourly fails"
  );

  sandbox.restore();
});

add_task(async function test_fetchHelper_small_size_skips_hourly() {
  const sandbox = sinon.createSandbox();

  sandbox.stub(WeatherFeed.prototype, "restartFetchTimer").returns(undefined);

  const feed = new WeatherFeed();
  feed.store = {
    dispatch: sinon.spy(),
    getState() {
      return {
        Prefs: {
          values: {
            "weather.display": "detailed",
            "widgets.system.weatherForecast.enabled": true,
            "widgets.weather.size": "small",
          },
        },
      };
    },
  };
  feed.merino = {
    fetchWeatherReport: sinon.stub().resolves({ city_name: "SidebarCity" }),
    fetchHourlyForecasts: sinon.stub().resolves([{ hour: 0 }]),
  };

  const results = await feed._fetchHelper(1, "q");

  Assert.equal(
    feed.merino.fetchHourlyForecasts.callCount,
    0,
    "hourly not fetched for small widget"
  );
  Assert.deepEqual(
    results,
    { suggestions: [{ city_name: "SidebarCity" }], hourlyForecasts: [] },
    "report returned without hourly data"
  );

  sandbox.restore();
});

add_task(async function test_isEnabled_classic_mode() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(WeatherFeed.prototype, "PersistentCache").returns({
    set: () => {},
    get: () => {},
  });

  let feed = new WeatherFeed();
  feed.store = {
    getState() {
      return {
        Prefs: {
          values: {
            showWeather: true,
            "system.showWeather": true,
            "nova.enabled": false,
            "widgets.weather.enabled": false,
          },
        },
      };
    },
  };

  Assert.ok(
    feed.isEnabled(),
    "isEnabled returns true when showWeather is true in classic mode"
  );

  feed.store.getState = () => ({
    Prefs: {
      values: {
        showWeather: false,
        "system.showWeather": true,
        "nova.enabled": false,
        "widgets.weather.enabled": true,
      },
    },
  });

  Assert.ok(
    !feed.isEnabled(),
    "isEnabled returns false when showWeather is false in classic mode"
  );

  sandbox.restore();
});

add_task(async function test_isEnabled_via_trainhop_namespace() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(WeatherFeed.prototype, "PersistentCache").returns({
    set: () => {},
    get: () => {},
  });

  let feed = new WeatherFeed();
  feed.store = {
    getState() {
      return {
        Prefs: {
          values: {
            "nova.enabled": true,
            "widgets.weather.enabled": true,
            trainhopConfig: { widgetWeather: { visible: true } },
          },
        },
      };
    },
  };

  Assert.ok(
    feed.isEnabled(),
    "isEnabled returns true via trainhopConfig.widgetWeather.visible"
  );

  sandbox.restore();
});

add_task(async function test_isEnabled_nova_mode() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(WeatherFeed.prototype, "PersistentCache").returns({
    set: () => {},
    get: () => {},
  });

  let feed = new WeatherFeed();
  feed.store = {
    getState() {
      return {
        Prefs: {
          values: {
            showWeather: true,
            "system.showWeather": true,
            "nova.enabled": true,
            "widgets.weather.enabled": true,
          },
        },
      };
    },
  };

  Assert.ok(
    feed.isEnabled(),
    "isEnabled returns true when widgets.weather.enabled is true in Nova mode"
  );

  feed.store.getState = () => ({
    Prefs: {
      values: {
        showWeather: true,
        "system.showWeather": true,
        "nova.enabled": true,
        "widgets.weather.enabled": false,
      },
    },
  });

  Assert.ok(
    !feed.isEnabled(),
    "isEnabled returns false when widgets.weather.enabled is false in Nova mode"
  );

  sandbox.restore();
});

add_task(async function test_onPrefChanged_widgets_weather_enabled() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(WeatherFeed.prototype, "PersistentCache").returns({
    set: () => {},
    get: () => {},
  });

  let feed = new WeatherFeed();
  feed.store = {
    dispatch: sinon.spy(),
    getState() {
      return {
        Prefs: {
          values: {
            showWeather: true,
            "system.showWeather": true,
            "nova.enabled": true,
            "widgets.weather.enabled": false,
          },
        },
      };
    },
  };
  feed.loaded = true;

  const resetWeatherStub = sandbox.stub(feed, "resetWeather").resolves();
  const loadWeatherStub = sandbox.stub(feed, "loadWeather").resolves();

  await feed.onPrefChangedAction({
    data: { name: "widgets.weather.enabled" },
  });

  Assert.ok(
    resetWeatherStub.calledOnce,
    "resetWeather called when widgets.weather.enabled is set to false"
  );
  Assert.ok(loadWeatherStub.notCalled, "loadWeather not called");

  feed.loaded = false;
  feed.store.getState = () => ({
    Prefs: {
      values: {
        showWeather: true,
        "system.showWeather": true,
        "nova.enabled": true,
        "widgets.weather.enabled": true,
      },
    },
  });

  await feed.onPrefChangedAction({
    data: { name: "widgets.weather.enabled" },
  });

  Assert.ok(
    loadWeatherStub.calledOnce,
    "loadWeather called when widgets.weather.enabled is set to true"
  );

  sandbox.restore();
});
