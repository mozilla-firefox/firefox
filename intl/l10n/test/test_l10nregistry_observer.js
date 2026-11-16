/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests for the intl:l10n-sources-changed observer notification fired by
 * L10nRegistry when sources are registered, updated, or removed at runtime
 * (Bug 2046945).
 */

"use strict";

const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

const TOPIC = "intl:l10n-sources-changed";

/**
 * Verifies that mutating the global L10nRegistry fires
 * intl:l10n-sources-changed once per call. Only the global registry
 * broadcasts; freshly-constructed (test-only) registries do not.
 */
add_task(async function test_topic_fires_on_global_registry_mutations() {
  const globalReg = L10nRegistry.getInstance();
  const SOURCE_NAME = "test-l10n-observer";

  const registered = TestUtils.topicObserved(TOPIC);
  globalReg.registerSources([
    new L10nFileSource(
      SOURCE_NAME,
      "test-l10n-observer-metasource",
      ["en-US"],
      "/test-l10n-observer/{locale}/"
    ),
  ]);
  await registered;
  Assert.ok(true, "Topic fired on registerSources");

  const updated = TestUtils.topicObserved(TOPIC);
  globalReg.updateSources([
    new L10nFileSource(
      SOURCE_NAME,
      "test-l10n-observer-metasource",
      ["en-US", "fr"],
      "/test-l10n-observer/{locale}/"
    ),
  ]);
  await updated;
  Assert.ok(true, "Topic fired on updateSources");

  const removed = TestUtils.topicObserved(TOPIC);
  globalReg.removeSources([SOURCE_NAME]);
  await removed;
  Assert.ok(true, "Topic fired on removeSources");
});

/**
 * Mutating sources whose locales are already available must not reorder the
 * app locales nor fire intl:app-locales-changed (Bug 2062777).
 */
add_task(async function test_no_spurious_app_locales_changed() {
  const globalReg = L10nRegistry.getInstance();
  const LOCALES = [
    "en-US",
    "en-AU",
    "en-CA",
    "en-GB",
    "en-IE",
    "en-IN",
    "en-NZ",
    "en-ZA",
  ];
  const METASOURCE = "test-app-locales-metasource";
  const makeSource = name =>
    new L10nFileSource(name, METASOURCE, LOCALES, `/${name}/{locale}/`);

  const originalRequested = Services.locale.requestedLocales;
  registerCleanupFunction(() => {
    globalReg.removeSources(["test-app-locales-base"]);
    Services.locale.requestedLocales = originalRequested;
  });

  globalReg.registerSources([makeSource("test-app-locales-base")]);
  Services.locale.requestedLocales = ["en-US"];
  const appLocales = Services.locale.appLocalesAsBCP47;
  Assert.deepEqual(
    appLocales.toSorted(),
    LOCALES.toSorted(),
    "All English variants are negotiated as app locales"
  );

  let fired = 0;
  const observer = () => fired++;
  Services.obs.addObserver(observer, "intl:app-locales-changed");

  for (let i = 0; i < 20; i++) {
    const name = `test-app-locales-${i}`;
    globalReg.registerSources([makeSource(name)]);
    globalReg.removeSources([name]);
  }

  Services.obs.removeObserver(observer, "intl:app-locales-changed");

  Assert.equal(fired, 0, "intl:app-locales-changed did not fire");
  Assert.deepEqual(
    Services.locale.appLocalesAsBCP47,
    appLocales,
    "App locales keep the same order"
  );
});
