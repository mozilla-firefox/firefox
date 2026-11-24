/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const tests = [];

for (let canonicalId of ["canonical", "canonical-001"]) {
  tests.push({
    locale: "en-US",
    region: "US",
    distribution: canonicalId,
    test: engines =>
      hasParams(engines, "Google", new Map([["default", "client=ubuntu"]])) &&
      hasParams(engines, "Google", new Map([["default", "channel=fs"]])) &&
      hasTelemetryId(engines, "Google", "google-canonical"),
  });

  tests.push({
    locale: "en-US",
    region: "GB",
    distribution: canonicalId,
    test: engines =>
      hasParams(engines, "Google", new Map([["default", "client=ubuntu"]])) &&
      hasParams(engines, "Google", new Map([["default", "channel=fs"]])) &&
      hasTelemetryId(engines, "Google", "google-canonical"),
  });
}

tests.push({
  locale: "en-US",
  region: "US",
  distribution: "canonical-002",
  test: engines =>
    hasParams(engines, "Google", new Map([["default", "client=ubuntu-sn"]])) &&
    hasParams(engines, "Google", new Map([["default", "channel=fs"]])) &&
    hasTelemetryId(engines, "Google", "google-ubuntu-sn"),
});

tests.push({
  locale: "en-US",
  region: "GB",
  distribution: "canonical-002",
  test: engines =>
    hasParams(engines, "Google", new Map([["default", "client=ubuntu-sn"]])) &&
    hasParams(engines, "Google", new Map([["default", "channel=fs"]])) &&
    hasTelemetryId(engines, "Google", "google-ubuntu-sn"),
});

tests.push({
  locale: "fr",
  distribution: "qwant-001",
  test: engines =>
    hasParams(
      engines,
      "Qwant",
      new Map([["default", "client=firefoxqwant"]])
    ) &&
    hasDefault(engines, "Qwant") &&
    hasEnginesFirst(engines, ["Qwant", "Qwant Junior"]),
});

tests.push({
  locale: "fr",
  distribution: "qwant-001",
  test: engines =>
    hasParams(
      engines,
      "Qwant Junior",
      new Map([["default", "client=firefoxqwant"]])
    ),
});

tests.push({
  locale: "fr",
  distribution: "qwant-002",
  test: engines =>
    hasParams(
      engines,
      "Qwant",
      new Map([["default", "client=firefoxqwant"]])
    ) &&
    hasDefault(engines, "Qwant") &&
    hasEnginesFirst(engines, ["Qwant", "Qwant Junior"]),
});

tests.push({
  locale: "fr",
  distribution: "qwant-002",
  test: engines =>
    hasParams(
      engines,
      "Qwant Junior",
      new Map([["default", "client=firefoxqwant"]])
    ),
});

for (const locale of ["en-US", "de"]) {
  tests.push({
    locale,
    distribution: "1und1",
    test: engines =>
      hasParams(engines, "1&1 Suche", new Map([["default", "enc=UTF-8"]])) &&
      hasDefault(engines, "1&1 Suche") &&
      hasEnginesFirst(engines, ["1&1 Suche"]),
  });

  tests.push({
    locale,
    distribution: "gmx",
    test: engines =>
      hasParams(engines, "GMX Suche", new Map([["default", "enc=UTF-8"]])) &&
      hasDefault(engines, "GMX Suche") &&
      hasEnginesFirst(engines, ["GMX Suche"]),
  });

  tests.push({
    locale,
    distribution: "gmx",
    test: engines =>
      hasParams(
        engines,
        "GMX Shopping",
        new Map([["default", "origin=br_osd"]])
      ),
  });

  tests.push({
    locale,
    distribution: "mail.com",
    test: engines =>
      hasParams(
        engines,
        "mail.com search",
        new Map([["default", "enc=UTF-8"]])
      ) &&
      hasDefault(engines, "mail.com search") &&
      hasEnginesFirst(engines, ["mail.com search"]),
  });

  tests.push({
    locale,
    distribution: "webde",
    test: engines =>
      hasParams(engines, "WEB.DE Suche", new Map([["default", "enc=UTF-8"]])) &&
      hasDefault(engines, "WEB.DE Suche") &&
      hasEnginesFirst(engines, ["WEB.DE Suche"]),
  });
}

tests.push({
  locale: "ru",
  region: "RU",
  distribution: "gmx",
  test: engines => hasDefault(engines, "GMX Suche"),
});

tests.push({
  locale: "en-GB",
  distribution: "gmxcouk",
  test: engines =>
    hasURLs(
      engines,
      "GMX Search",
      "https://go.gmx.co.uk/br/moz_search_web/?enc=UTF-8&q=test",
      "https://suggestplugin.gmx.co.uk/s?brand=gmxcouk&origin=moz_splugin_ff&enc=UTF-8&q=test"
    ) &&
    hasDefault(engines, "GMX Search") &&
    hasEnginesFirst(engines, ["GMX Search"]),
});

tests.push({
  locale: "ru",
  region: "RU",
  distribution: "gmxcouk",
  test: engines => hasDefault(engines, "GMX Search"),
});

tests.push({
  locale: "es",
  distribution: "gmxes",
  test: engines =>
    hasURLs(
      engines,
      "GMX - Búsqueda web",
      "https://go.gmx.es/br/moz_search_web/?enc=UTF-8&q=test",
      "https://suggestplugin.gmx.es/s?brand=gmxes&origin=moz_splugin_ff&enc=UTF-8&q=test"
    ) &&
    hasDefault(engines, "GMX - Búsqueda web") &&
    hasEnginesFirst(engines, ["GMX - Búsqueda web"]),
});

tests.push({
  locale: "ru",
  region: "RU",
  distribution: "gmxes",
  test: engines => hasDefault(engines, "GMX - Búsqueda web"),
});

tests.push({
  locale: "fr",
  distribution: "gmxfr",
  test: engines =>
    hasURLs(
      engines,
      "GMX - Recherche web",
      "https://go.gmx.fr/br/moz_search_web/?enc=UTF-8&q=test",
      "https://suggestplugin.gmx.fr/s?brand=gmxfr&origin=moz_splugin_ff&enc=UTF-8&q=test"
    ) &&
    hasDefault(engines, "GMX - Recherche web") &&
    hasEnginesFirst(engines, ["GMX - Recherche web"]),
});

tests.push({
  locale: "ru",
  region: "RU",
  distribution: "gmxfr",
  test: engines => hasDefault(engines, "GMX - Recherche web"),
});

tests.push({
  locale: "en-US",
  region: "US",
  distribution: "mint-001",
  test: engines =>
    hasParams(engines, "DuckDuckGo", new Map([["default", "t=lm"]])) &&
    hasParams(
      engines,
      "Google",
      new Map([["default", "client=firefox-b-1-lm"]])
    ) &&
    hasDefault(engines, "Google") &&
    hasEnginesFirst(engines, ["Google"]) &&
    hasTelemetryId(engines, "Google", "google-b-1-lm"),
});

tests.push({
  locale: "en-GB",
  region: "GB",
  distribution: "mint-001",
  test: engines =>
    hasParams(engines, "DuckDuckGo", new Map([["default", "t=lm"]])) &&
    hasParams(
      engines,
      "Google",
      new Map([["default", "client=firefox-b-lm"]])
    ) &&
    hasDefault(engines, "Google") &&
    hasEnginesFirst(engines, ["Google"]) &&
    hasTelemetryId(engines, "Google", "google-b-lm"),
});

tests.push({
  region: "ru",
  distribution: "mint-001",
  test: engines =>
    hasDefault(engines, "Google") &&
    hasEnginesFirst(engines, ["Google"]) &&
    hasTelemetryId(engines, "Google", "google-com-nocodes"),
});

if (SearchUtils.configV3FeatureGate) {
  // Android distributions all use the same Google codes as the main version for
  // search-config-v3.
  let googleChecksForV3 = engines =>
    hasParams(
      engines,
      "Google",
      new Map([
        ["default", "client=android-firefox-o-re"],
        ["topsite", "client=android-firefox-b-re"],
        ["context", "client=android-firefox-cm-re"],
        ["widget", "client=android-firefox-msw-re"],
      ])
    ) &&
    hasDefault(engines, "Google") &&
    hasEnginesFirst(engines, ["Google"]) &&
    hasTelemetryId(engines, "Google", "google-android-main");

  // This distribution is used on mobile, but we can test it here as it only
  // needs the distribution id referencing.
  tests.push({
    locale: "en-GB",
    region: "GB",
    distribution: "vivo-001",
    platform: "android",
    test: engines =>
      hasParams(engines, "Bing", new Map([["default", "pc=MZCP"]])) &&
      hasParams(engines, "Bing", new Map([["default", "form=MZVIVO"]])) &&
      hasTelemetryId(engines, "Bing", "bing-MZCP") &&
      googleChecksForV3(engines),
  });

  tests.push({
    region: "ru",
    distribution: "vivo-001",
    platform: "android",
    test: engines =>
      hasDefault(engines, "Google") &&
      hasEnginesFirst(engines, ["Google"]) &&
      hasTelemetryId(engines, "Google", "google-com-nocodes"),
  });

  // This distribution is used on mobile, but we can test it here as it only
  // needs the distribution id referencing.
  tests.push({
    locale: "en-GB",
    region: "GB",
    distribution: "dt-001",
    platform: "android",
    test: engines => googleChecksForV3(engines),
  });

  // This distribution is used on mobile, but we can test it here as it only
  // needs the distribution id referencing.
  tests.push({
    locale: "en-GB",
    region: "GB",
    distribution: "dt-001",
    platform: "android",
    test: engines =>
      hasParams(engines, "Bing", new Map([["default", "pc=MZTOF"]])) &&
      hasParams(engines, "Bing", new Map([["default", "form=MZTOFO"]])) &&
      hasDefault(engines, "Google") &&
      hasEnginesFirst(engines, ["Google"]) &&
      hasTelemetryId(engines, "Bing", "bing-MZTOF"),
  });

  tests.push({
    region: "ru",
    distribution: "dt-001",
    platform: "android",
    test: engines =>
      hasDefault(engines, "Google") &&
      hasEnginesFirst(engines, ["Google"]) &&
      hasTelemetryId(engines, "Google", "google-com-nocodes"),
  });

  tests.push({
    region: "US",
    distribution: "dt-002",
    platform: "android",
    test: engines => googleChecksForV3(engines),
  });

  tests.push({
    region: "CA", // Testing for "rest of world" (excluding US, RU, BY)
    distribution: "dt-002",
    platform: "android",
    test: engines => googleChecksForV3(engines),
  });

  tests.push({
    region: "US",
    distribution: "dt-003",
    platform: "android",
    test: engines => googleChecksForV3(engines),
  });

  tests.push({
    region: "CA", // Testing for "rest of world" (excluding US, RU, BY)
    distribution: "dt-003",
    platform: "android",
    test: engines => googleChecksForV3(engines),
  });

  tests.push({
    region: "US",
    distribution: "xiaomi-001",
    platform: "android",
    test: engines => googleChecksForV3(engines),
  });

  tests.push({
    region: "CA", // Testing for "rest of world" (excluding US, RU, BY)
    distribution: "xiaomi-001",
    platform: "android",
    test: engines => googleChecksForV3(engines),
  });

  tests.push({
    region: "US",
    distribution: "aura-001",
    platform: "android",
    test: engines => googleChecksForV3(engines),
  });

  tests.push({
    region: "CA", // Testing for "rest of world" (excluding US, RU, TR, BY, KZ)
    distribution: "aura-001",
    platform: "android",
    test: engines => googleChecksForV3(engines),
  });
} else {
  // V2 specific tests

  // This distribution is used on mobile, but we can test it here as it only
  // needs the distribution id referencing.
  tests.push({
    locale: "en-GB",
    region: "GB",
    distribution: "vivo-001",
    application: "firefox-android",
    test: engines =>
      hasParams(engines, "Bing", new Map([["default", "pc=MZCP"]])) &&
      hasParams(engines, "Bing", new Map([["default", "form=MZVIVO"]])) &&
      hasTelemetryId(engines, "Bing", "bing-MZCP") &&
      hasParams(
        engines,
        "Google",
        new Map([["default", "client=firefox-b-vv"]])
      ) &&
      hasDefault(engines, "Google") &&
      hasEnginesFirst(engines, ["Google"]) &&
      hasTelemetryId(engines, "Google", "google-b-vv"),
  });

  tests.push({
    region: "ru",
    distribution: "vivo-001",
    application: "firefox-android",
    test: engines =>
      hasDefault(engines, "Google") &&
      hasEnginesFirst(engines, ["Google"]) &&
      hasTelemetryId(engines, "Google", "google-com-nocodes"),
  });

  // This distribution is used on mobile, but we can test it here as it only
  // needs the distribution id referencing.
  tests.push({
    locale: "en-GB",
    region: "GB",
    distribution: "dt-001",
    application: "firefox-android",
    test: engines =>
      hasParams(
        engines,
        "Google",
        new Map([["default", "client=firefox-b-tf"]])
      ) &&
      hasDefault(engines, "Google") &&
      hasEnginesFirst(engines, ["Google"]) &&
      hasTelemetryId(engines, "Google", "google-b-tf"),
  });

  // This distribution is used on mobile, but we can test it here as it only
  // needs the distribution id referencing.
  tests.push({
    locale: "en-GB",
    region: "GB",
    distribution: "dt-001",
    application: "firefox-android",
    test: engines =>
      hasParams(engines, "Bing", new Map([["default", "pc=MZTOF"]])) &&
      hasParams(engines, "Bing", new Map([["default", "form=MZTOFO"]])) &&
      hasDefault(engines, "Google") &&
      hasEnginesFirst(engines, ["Google"]) &&
      hasTelemetryId(engines, "Bing", "bing-MZTOF"),
  });

  tests.push({
    region: "ru",
    distribution: "dt-001",
    application: "firefox-android",
    test: engines =>
      hasDefault(engines, "Google") &&
      hasEnginesFirst(engines, ["Google"]) &&
      hasTelemetryId(engines, "Google", "google-com-nocodes"),
  });

  tests.push({
    region: "US",
    distribution: "dt-002",
    application: "firefox-android",
    test: engines =>
      hasParams(
        engines,
        "Google",
        new Map([["default", "client=firefox-b-1-dt"]])
      ) &&
      hasDefault(engines, "Google") &&
      hasEnginesFirst(engines, ["Google"]) &&
      hasTelemetryId(engines, "Google", "google-b-1-dt"),
  });

  tests.push({
    region: "CA", // Testing for "rest of world" (excluding US, RU, BY)
    distribution: "dt-002",
    application: "firefox-android",
    test: engines =>
      hasParams(
        engines,
        "Google",
        new Map([["default", "client=firefox-b-dt"]])
      ) &&
      hasDefault(engines, "Google") &&
      hasEnginesFirst(engines, ["Google"]) &&
      hasTelemetryId(engines, "Google", "google-b-dt"),
  });

  tests.push({
    region: "US",
    distribution: "dt-003",
    application: "firefox-android",
    test: engines =>
      hasParams(
        engines,
        "Google",
        new Map([["default", "client=firefox-b-1-dt"]])
      ) &&
      hasDefault(engines, "Google") &&
      hasEnginesFirst(engines, ["Google"]) &&
      hasTelemetryId(engines, "Google", "google-b-1-dt"),
  });

  tests.push({
    region: "CA", // Testing for "rest of world" (excluding US, RU, BY)
    distribution: "dt-003",
    application: "firefox-android",
    test: engines =>
      hasParams(
        engines,
        "Google",
        new Map([["default", "client=firefox-b-dt"]])
      ) &&
      hasDefault(engines, "Google") &&
      hasEnginesFirst(engines, ["Google"]) &&
      hasTelemetryId(engines, "Google", "google-b-dt"),
  });

  tests.push({
    region: "US",
    distribution: "xiaomi-001",
    application: "firefox-android",
    test: engines =>
      hasParams(
        engines,
        "Google",
        new Map([["default", "client=firefox-b-1-dt"]])
      ) &&
      hasDefault(engines, "Google") &&
      hasEnginesFirst(engines, ["Google"]) &&
      hasTelemetryId(engines, "Google", "google-b-1-dt"),
  });

  tests.push({
    region: "CA", // Testing for "rest of world" (excluding US, RU, BY)
    distribution: "xiaomi-001",
    application: "firefox-android",
    test: engines =>
      hasParams(
        engines,
        "Google",
        new Map([["default", "client=firefox-b-dt"]])
      ) &&
      hasDefault(engines, "Google") &&
      hasEnginesFirst(engines, ["Google"]) &&
      hasTelemetryId(engines, "Google", "google-b-dt"),
  });

  tests.push({
    region: "US",
    distribution: "aura-001",
    application: "firefox-android",
    test: engines =>
      hasParams(
        engines,
        "Google",
        new Map([["default", "client=firefox-b-1-ar"]])
      ) &&
      hasDefault(engines, "Google") &&
      hasEnginesFirst(engines, ["Google"]) &&
      hasTelemetryId(engines, "Google", "google-b-1-ar"),
  });

  tests.push({
    region: "CA", // Testing for "rest of world" (excluding US, RU, TR, BY, KZ)
    distribution: "aura-001",
    application: "firefox-android",
    test: engines =>
      hasParams(
        engines,
        "Google",
        new Map([["default", "client=firefox-b-ar"]])
      ) &&
      hasDefault(engines, "Google") &&
      hasEnginesFirst(engines, ["Google"]) &&
      hasTelemetryId(engines, "Google", "google-b-ar"),
  });
}

function hasURLs(engines, engineName, url, suggestURL) {
  let engine = engines.find(e => e.name === engineName);
  Assert.ok(engine, `Should be able to find ${engineName}`);

  let submission = engine.getSubmission("test", "text/html");
  Assert.equal(
    submission.uri.spec,
    url,
    `Should have the correct submission url for ${engineName}`
  );

  submission = engine.getSubmission("test", "application/x-suggestions+json");
  Assert.equal(
    submission.uri.spec,
    suggestURL,
    `Should have the correct suggestion url for ${engineName}`
  );
  return true;
}

function hasParams(engines, engineName, paramMap) {
  let engine = engines.find(e => e.name === engineName);
  Assert.ok(engine, `Should be able to find ${engineName}`);

  for (let [sap, paramDetails] of paramMap) {
    let submission = engine.getSubmission(
      "test",
      "text/html",
      sap == "default" ? "urlbar" : sap
    );
    let queries = URL.fromURI(submission.uri).searchParams;

    let foundParamNames = new Set();
    for (let [key] of queries) {
      Assert.ok(
        !foundParamNames.has(key),
        `Should not have a duplicate ${key} param`
      );
      foundParamNames.add(key);
    }

    let result = queries.toString().split("&").includes(paramDetails);
    Assert.ok(
      result,
      `expect ${submission.uri.query} to include ${paramDetails}`
    );
  }
  return true;
}

function doesNotHaveParams(engines, engineName, paramNames) {
  let engine = engines.find(e => e.name === engineName);
  Assert.ok(engine, `Should be able to find ${engineName}`);

  let submission = engine.getSubmission("test", "text/html", "urlbar");
  let queries = URL.fromURI(submission.uri).searchParams;

  let foundParamNames = new Set();
  for (let [key] of queries) {
    Assert.ok(
      !foundParamNames.has(key),
      `Should not have a duplicate ${key} param`
    );
    foundParamNames.add(key);

    Assert.ok(!paramNames.includes(key), `Should not include ${key} param`);
  }

  return true;
}

function hasTelemetryId(engines, engineName, telemetryId) {
  let engine = engines.find(e => e.name === engineName);
  Assert.ok(engine, `Should be able to find ${engineName}`);

  Assert.equal(
    engine.telemetryId,
    telemetryId,
    "Should have the correct telemetryId"
  );
  return true;
}

function hasDefault(engines, expectedDefaultName) {
  Assert.equal(
    engines[0].name,
    expectedDefaultName,
    "Should have the expected engine set as default"
  );
  return true;
}

function hasEnginesFirst(engines, expectedEngines) {
  for (let [i, expectedEngine] of expectedEngines.entries()) {
    Assert.equal(
      engines[i].name,
      expectedEngine,
      `Should have the expected engine in position ${i}`
    );
  }
  return true;
}

add_setup(async function () {
  updateAppInfo({
    name: "firefox",
    ID: "xpcshell@tests.mozilla.org",
    version: "128",
    platformVersion: "128",
  });

  await maybeSetupConfig();
});

add_task(async function test_expected_distribution_engines() {
  let engineSelector = new SearchEngineSelector();

  for (const {
    application = "firefox",
    platform = "win",
    distribution,
    locale = "en-US",
    region = "US",
    test,
  } of tests) {
    let config = await engineSelector.fetchEngineConfiguration({
      locale,
      region,
      distroID: distribution,
      appName: application,
      platform,
    });

    let engines = await SearchTestUtils.searchConfigToEngines(config.engines);
    engines = SearchUtils.sortEnginesByDefaults({
      engines,
      appDefaultEngine: engines[0],
    });
    test(engines);
  }
});
