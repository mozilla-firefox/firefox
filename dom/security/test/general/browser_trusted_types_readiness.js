"use strict";

const PAGE =
  getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content",
    "https://example.com"
  ) + "file_trusted_types_readiness.sjs";

const PREF = "dom.security.trusted_types.readiness_telemetry.enabled";

function snapshot() {
  const labels = Glean.security.trustedTypesReadiness.testGetValue() ?? {};
  return {
    labels,
    destroyed: Glean.useCounter.contentDocumentsDestroyed.testGetValue() ?? 0,
  };
}

async function loadAndDestroy(query, expectedLabel) {
  await Services.fog.testFlushAllChildren();
  const before = snapshot();
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    `${PAGE}?${query}`
  );
  await BrowserTestUtils.removeTab(tab);
  let after;
  const delta = label =>
    (after.labels[label] ?? 0) - (before.labels[label] ?? 0);
  // Document destruction, where reporting happens, lags the tab removal. Wait
  // on the specific label this case expects rather than on the shared
  // contentDocumentsDestroyed counter, which an unrelated document could bump
  // before our page's report is flushed. Cases that expect no label (the
  // pref-off case) fall back to waiting on the destroyed counter.
  await TestUtils.waitForCondition(async () => {
    await Services.fog.testFlushAllChildren();
    after = snapshot();
    return expectedLabel
      ? delta(expectedLabel) >= 1
      : after.destroyed > before.destroyed;
  }, "Waiting for the document to be destroyed and counted");
  const total = Object.keys(after.labels).reduce((s, l) => s + delta(l), 0);
  Assert.lessOrEqual(
    total,
    after.destroyed - before.destroyed,
    "At most one label per destroyed document"
  );
  Assert.equal(delta("__other__"), 0, "No invalid label recorded");
  return delta;
}

add_task(async function test_pref_off() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, false]] });
  const delta = await loadAndDestroy("csp=none&script=innerhtml", null);
  Assert.equal(
    delta(
      "htmlsink-string_writesink-none_ttcsp-none_strictcsp-none_created-0_used-0_lib-0_js-1"
    ),
    0,
    "Nothing recorded with the pref off"
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_readiness() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  for (const [query, label] of [
    [
      "csp=none&script=plain",
      "htmlsink-none_writesink-none_ttcsp-none_strictcsp-none_created-0_used-0_lib-0_js-0",
    ],
    [
      "csp=none&script=plain&handler=1",
      "htmlsink-none_writesink-none_ttcsp-none_strictcsp-none_created-0_used-0_lib-0_js-1",
    ],
    [
      "csp=none&script=empty",
      "htmlsink-none_writesink-none_ttcsp-none_strictcsp-none_created-0_used-0_lib-0_js-0",
    ],
    [
      "csp=none&script=innerhtml",
      "htmlsink-string_writesink-none_ttcsp-none_strictcsp-none_created-0_used-0_lib-0_js-1",
    ],
    ...[
      "sethtmlunsafe",
      "shadow_sethtmlunsafe",
      "parsehtmlunsafe",
      "contextualfragment",
      "domparser",
      "srcdoc_property",
      "srcdoc_attribute",
      "data_document",
    ].map(script => [
      `csp=none&script=${script}`,
      "htmlsink-string_writesink-none_ttcsp-none_strictcsp-none_created-0_used-0_lib-0_js-1",
    ]),
    [
      "csp=none&script=innerhtml&module=1",
      "htmlsink-string_writesink-none_ttcsp-none_strictcsp-none_created-0_used-0_lib-0_js-1",
    ],
    [
      "csp=none&script=trusted_html&module=1",
      "htmlsink-trusted_writesink-none_ttcsp-none_strictcsp-none_created-1_used-1_lib-0_js-1",
    ],
    [
      "csp=enforce&script=write_default",
      "htmlsink-none_writesink-string_ttcsp-enforce_strictcsp-none_created-1_used-1_lib-0_js-1",
    ],
    [
      "csp=report&script=policy_only",
      "htmlsink-none_writesink-none_ttcsp-report_strictcsp-none_created-1_used-0_lib-0_js-1",
    ],
    [
      "csp=none&script=create_html",
      "htmlsink-none_writesink-none_ttcsp-none_strictcsp-none_created-1_used-1_lib-0_js-1",
    ],
    [
      "csp=none&script=trusted_html",
      "htmlsink-trusted_writesink-none_ttcsp-none_strictcsp-none_created-1_used-1_lib-0_js-1",
    ],
    [
      "csp=none&script=write_noargs",
      "htmlsink-none_writesink-none_ttcsp-none_strictcsp-none_created-0_used-0_lib-0_js-1",
    ],
    [
      "csp=none&script=mixed_write",
      "htmlsink-trusted_writesink-string_ttcsp-none_strictcsp-none_created-1_used-1_lib-0_js-1",
    ],
    [
      "csp=none&strict=enforce&script=plain",
      "htmlsink-none_writesink-none_ttcsp-none_strictcsp-enforce_created-0_used-0_lib-0_js-0",
    ],
    [
      "csp=enforce&strict=enforce&script=trusted_html",
      "htmlsink-trusted_writesink-none_ttcsp-enforce_strictcsp-enforce_created-1_used-1_lib-0_js-1",
    ],
    [
      "csp=none&strict=report&script=innerhtml",
      "htmlsink-string_writesink-none_ttcsp-none_strictcsp-report_created-0_used-0_lib-0_js-1",
    ],
    [
      "csp=none&strict=nobase&script=create_html",
      "htmlsink-none_writesink-none_ttcsp-none_strictcsp-none_created-1_used-1_lib-0_js-1",
    ],
    [
      "csp=none&strict=self&script=trusted_html",
      "htmlsink-trusted_writesink-none_ttcsp-none_strictcsp-none_created-1_used-1_lib-0_js-1",
    ],
    [
      "csp=enforce&strict=report&script=trusted_html",
      "htmlsink-trusted_writesink-none_ttcsp-enforce_strictcsp-report_created-1_used-1_lib-0_js-1",
    ],
    [
      "csp=report&strict=enforce&script=policy_only",
      "htmlsink-none_writesink-none_ttcsp-report_strictcsp-enforce_created-1_used-0_lib-0_js-1",
    ],
  ]) {
    const delta = await loadAndDestroy(query, label);
    Assert.greaterOrEqual(delta(label), 1, `${query} records ${label}`);
  }
  await SpecialPowers.popPrefEnv();
});
