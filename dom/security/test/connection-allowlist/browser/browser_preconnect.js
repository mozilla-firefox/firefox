"use strict";

const TEST_PATH = getRootDirectory(gTestPath).replace(
  "chrome://mochitests/content",
  "https://example.com"
);

// Speculative connections are skipped for proxied URLs, so use hosts that the
// mochitest proxy doesn't know about.
const ALLOWED = "https://127.0.0.1";
const BLOCKED = "https://127.0.0.4";
// Allowed by "https://127.0.0.5/*" either way, and preconnected to last so we
// know when the other preconnects have been handled.
const LAST = "https://127.0.0.5";
const ALLOWED_HOST_PORT = "127.0.0.1:443";
const BLOCKED_HOST_PORT = "127.0.0.4:443";
const LAST_HOST_PORT = "127.0.0.5:443";

// Only the origin of a preconnect is matched against the allowlist patterns
// "https://127.0.0.1/" and "https://127.0.0.4/some/*". That allows ALLOWED and
// blocks BLOCKED, while matching these full URLs would do the opposite.
const HREFS = [
  `${ALLOWED}/some/path?query`,
  `${BLOCKED}/some/path?query`,
  `${LAST}/`,
];

// Adds <link rel=preconnect> elements for HREFS in order. Resolves with the
// connection hash keys of every speculative connection seen until the one to
// LAST, and the first batch of Connection-Allowlist reports.
async function preconnect(browser) {
  const seen = [];
  const done = new Promise(resolve => {
    const observer = {
      QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),
      observe(aSubject, aTopic, aData) {
        info(`Speculative connection: ${aData}`);
        seen.push(aData);
        if (aData.includes(LAST_HOST_PORT)) {
          Services.obs.removeObserver(observer, "speculative-connect-request");
          resolve();
        }
      },
    };
    Services.obs.addObserver(observer, "speculative-connect-request");
  });

  const reports = await SpecialPowers.spawn(browser, [HREFS], aHrefs => {
    const reportsPromise = new Promise(resolve => {
      const observer = new content.ReportingObserver(
        aReports => {
          observer.disconnect();
          resolve(JSON.stringify(aReports));
        },
        { types: ["connection-allowlist"] }
      );
      observer.observe();
    });

    for (const href of aHrefs) {
      const link = content.document.createElement("link");
      link.rel = "preconnect";
      link.href = href;
      content.document.head.appendChild(link);
    }

    return reportsPromise;
  });

  await done;
  return { seen, reports: JSON.parse(reports) };
}

function checkReport(reports, disposition) {
  is(reports.length, 1, "Only the not-allow-listed origin is reported");
  const { type, body } = reports[0];
  is(type, "connection-allowlist", "Report type");
  is(
    body.url,
    TEST_PATH +
      `file_preconnect${disposition == "report" ? "_report_only" : ""}.html`,
    "Report url"
  );
  is(body.connection, `${BLOCKED}/`, "The reported connection is the origin");
  Assert.deepEqual(
    body.allowlist,
    [`${ALLOWED}/`, `${BLOCKED}/some/*`, `${LAST}/*`],
    "Report allowlist"
  );
  is(body.disposition, disposition, "Report disposition");
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["network.http.debug-observations", true]],
  });
});

add_task(async function test_enforce() {
  await BrowserTestUtils.withNewTab(
    TEST_PATH + "file_preconnect.html",
    async browser => {
      const { seen, reports } = await preconnect(browser);
      ok(
        seen.some(key => key.includes(ALLOWED_HOST_PORT)),
        `Speculative connection to the allowed origin, saw ${seen}`
      );
      ok(
        !seen.some(key => key.includes(BLOCKED_HOST_PORT)),
        `No speculative connection to the blocked origin, saw ${seen}`
      );
      checkReport(reports, "enforce");
    }
  );
});

add_task(async function test_report_only() {
  await BrowserTestUtils.withNewTab(
    TEST_PATH + "file_preconnect_report_only.html",
    async browser => {
      const { seen, reports } = await preconnect(browser);
      ok(
        seen.some(key => key.includes(ALLOWED_HOST_PORT)),
        `Speculative connection to the allowed origin, saw ${seen}`
      );
      ok(
        seen.some(key => key.includes(BLOCKED_HOST_PORT)),
        `Report-only does not block, saw ${seen}`
      );
      checkReport(reports, "report");
    }
  );
});
