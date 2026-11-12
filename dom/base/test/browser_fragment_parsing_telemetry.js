"use strict";

const PREF = "dom.html_fragment_fast_path.enabled";

function counts(metric, labels) {
  return Object.fromEntries(
    labels.map(label => [label, metric[label].testGetValue()?.count ?? 0])
  );
}

const TOTAL_LABELS = [
  "fast_under_1k",
  "fast_under_10k",
  "fallback_under_1k",
  "full_under_1k",
  "full_under_10k",
];
const REJECTED_LABELS = ["under_1k", "under_10k"];

add_task(async function test_fragment_parsing_timing() {
  if (Services.prefs.getBoolPref("telemetry.fog.artifact_build", false)) {
    Assert.ok(true, "Test skipped in artifact builds. See bug 1836686.");
    return;
  }

  for (const enabled of [true, false]) {
    await SpecialPowers.pushPrefEnv({
      set: [
        [PREF, enabled],
        // Time every call.
        ["dom.html_fragment_fast_path.telemetry_sample_shift", 0],
      ],
    });
    const parser = new DOMParser();
    const doc = parser.parseFromString("", "text/html");
    const div = doc.createElement("div");
    await Services.fog.testFlushAllChildren();
    Services.fog.testResetFOG();

    // Keep parsing and assertions synchronous so unrelated calls cannot interfere.
    // Short text without markup isn't parsed, so it isn't recorded.
    div.innerHTML = "plain text";
    div.innerHTML = "<span class=a>x</span>";
    div.innerHTML = "<table><tr><td>x</td></tr></table>";
    div.innerHTML = `<span>${"x".repeat(2000)}</span>`;
    parser.parseFromString("<span class=a>x</span>", "text/html");
    parser.parseFromString("<table><tr><td>x</td></tr></table>", "text/html");

    const metrics = Glean.domFragmentParsing;
    Assert.deepEqual(
      counts(metrics.innerHtml, TOTAL_LABELS),
      enabled
        ? {
            fast_under_1k: 1,
            fast_under_10k: 1,
            fallback_under_1k: 1,
            full_under_1k: 0,
            full_under_10k: 0,
          }
        : {
            fast_under_1k: 0,
            fast_under_10k: 0,
            fallback_under_1k: 0,
            full_under_1k: 2,
            full_under_10k: 1,
          },
      `innerHTML parse times with the fast path ${enabled}`
    );
    Assert.deepEqual(
      counts(metrics.innerHtmlFastPathRejected, REJECTED_LABELS),
      { under_1k: enabled ? 1 : 0, under_10k: 0 },
      `innerHTML fast path rejections with the fast path ${enabled}`
    );
    Assert.deepEqual(
      counts(metrics.domParser, TOTAL_LABELS),
      enabled
        ? {
            fast_under_1k: 1,
            fast_under_10k: 0,
            fallback_under_1k: 1,
            full_under_1k: 0,
            full_under_10k: 0,
          }
        : {
            fast_under_1k: 0,
            fast_under_10k: 0,
            fallback_under_1k: 0,
            full_under_1k: 2,
            full_under_10k: 0,
          },
      `DOMParser parse times with the fast path ${enabled}`
    );
    Assert.deepEqual(
      counts(metrics.domParserFastPathRejected, REJECTED_LABELS),
      { under_1k: enabled ? 1 : 0, under_10k: 0 },
      `DOMParser fast path rejections with the fast path ${enabled}`
    );

    await SpecialPowers.popPrefEnv();
  }
});
