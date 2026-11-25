/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Deprecation reports are exposed to web content, so they must use en-US
// instead of the app locale when English spoofing is enabled.

const TEST_URL = "https://example.com/browser/dom/reporting/tests/empty.html";
const L10N_URL = getRootDirectory(gTestPath) + "localization/{locale}/";
const L10N_SOURCE = "test-deprecated-fluent";
const TEST_LOCALE = "it";

const CASES = [
  {
    method: "captureEvents",
    l10nId: "dom-deprecated-capture-events",
    args: {
      url: "https://developer.mozilla.org/docs/Web/API/EventTarget/addEventListener",
    },
  },
  {
    method: "releaseEvents",
    l10nId: "dom-deprecated-release-events",
    args: {
      url: "https://developer.mozilla.org/docs/Web/API/EventTarget/removeEventListener",
    },
  },
  {
    // Reported from a worker, which decides on its own whether to spoof.
    worker: "worker_deprecated_fluent.js",
    l10nId: "dom-deprecated-idb-object-store-moz-get-all",
    args: {},
  },
];

function formatExpected(locale, { l10nId, args }) {
  const l10n = new Localization(["dom/deprecations.ftl"], true, undefined, [
    locale,
  ]);
  return l10n.formatValueSync(l10nId, args);
}

add_setup(async function () {
  const originalAvailableLocales = Services.locale.availableLocales;
  const originalRequestedLocales = Services.locale.requestedLocales;

  // Sources must be registered before the app locales change, as cached
  // Localization objects only refresh on intl:app-locales-changed.
  L10nRegistry.getInstance().registerSources([
    new L10nFileSource(L10N_SOURCE, "app", [TEST_LOCALE], L10N_URL),
  ]);
  Services.locale.availableLocales = [TEST_LOCALE, "en-US"];
  Services.locale.requestedLocales = [TEST_LOCALE];

  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.reporting.enabled", true],
      ["dom.reporting.testing.enabled", true],
    ],
  });

  registerCleanupFunction(() => {
    Services.locale.requestedLocales = originalRequestedLocales;
    Services.locale.availableLocales = originalAvailableLocales;
    L10nRegistry.getInstance().removeSources([L10N_SOURCE]);
  });
});

async function getReportMessages() {
  return BrowserTestUtils.withNewTab(TEST_URL, browser =>
    SpecialPowers.spawn(browser, [CASES], async cases => {
      const messages = [];
      for (const { method, worker } of cases) {
        if (worker) {
          const w = new content.Worker(worker);
          const { message } = await new Promise(resolve => {
            w.onmessage = e => resolve(e.data);
            w.postMessage(null);
          });
          w.terminate();
          messages.push(message);
          continue;
        }
        const report = await new Promise(resolve => {
          const obs = new content.ReportingObserver(reports => {
            obs.disconnect();
            resolve(reports[0]);
          });
          obs.observe();
          content.document[method]();
        });
        messages.push(report.body.message);
      }
      return messages;
    })
  );
}

for (const spoof of [false, true]) {
  add_task(async function test_deprecation_report_locale() {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["privacy.resistFingerprinting", spoof],
        ["privacy.spoof_english", spoof ? 2 : 0],
      ],
    });

    const messages = await getReportMessages();
    CASES.forEach((testCase, i) => {
      const english = formatExpected("en-US", testCase);
      const localized = formatExpected(TEST_LOCALE, testCase);
      const name = testCase.method ?? testCase.worker;
      isnot(english, localized, `${name}: test locale is distinct`);
      is(
        messages[i],
        spoof ? english : localized,
        `${name}: report uses ${spoof ? "en-US" : TEST_LOCALE}`
      );
    });

    await SpecialPowers.popPrefEnv();
  });
}
