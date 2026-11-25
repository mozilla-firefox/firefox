/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const TRAVEL = {
  label: "Travel details",
  favicon: "page-icon:https://example.org/",
};
const CONTACT = {
  label: "Contact details",
  favicon: "page-icon:https://example.net/",
};
const BOOKING = {
  label: "Booking details",
  favicon: "page-icon:https://example.com/",
};

let row;

add_setup(function setup_autocomplete_row() {
  row = document.createElement("autocomplete-row-item");
  row.type = "smartFormFill";
  row.hidden = true;
  document.documentElement.appendChild(row);
  registerCleanupFunction(() => row.remove());

  if (!row.updateComplete) {
    throw new Error("The autocomplete row custom element did not initialize");
  }
});

add_task(async function test_no_pills_without_selected_sources() {
  const emptySourcesLabel = "ai-smart-form-fill-autocomplete-choose-tabs";
  const root = await renderSources([], { emptySourcesLabel });

  Assert.equal(root.querySelectorAll(".sources-pill").length, 0);
  Assert.equal(
    root.querySelector(".smart-form-fill-sources")?.textContent.trim(),
    emptySourcesLabel,
    "When no sources are selected, the row shows the supplied empty-state label"
  );
});

add_task(async function test_one_source_renders_one_named_pill() {
  const root = await renderSources([TRAVEL]);

  Assert.equal(root.querySelectorAll(".sources-pill").length, 1);
  const pill = root.querySelector(".sources-pill-named");
  Assert.equal(
    pill?.querySelector(".source-label")?.textContent,
    TRAVEL.label,
    "The named pill shows the source name"
  );
  Assert.equal(
    pill?.querySelector("img")?.getAttribute("src"),
    TRAVEL.favicon,
    "The named pill shows the matching favicon"
  );
});

add_task(async function test_two_sources_render_two_named_pills() {
  const root = await renderSources([TRAVEL, CONTACT]);

  Assert.equal(root.querySelectorAll(".sources-pill").length, 2);
  const pills = [...root.querySelectorAll(".sources-pill-named")];
  Assert.deepEqual(
    pills.map(pill => pill.querySelector(".source-label")?.textContent),
    [TRAVEL.label, CONTACT.label],
    "The named pills show the source names in order"
  );
  Assert.deepEqual(
    pills.map(pill => pill.querySelector("img")?.getAttribute("src")),
    [TRAVEL.favicon, CONTACT.favicon],
    "The named pills show the matching favicons"
  );
});

add_task(async function test_three_sources_render_one_summary_pill() {
  const sourcesPillsLabel = "ai-smart-form-fill-autocomplete-tabs-count";
  const root = await renderSources([TRAVEL, CONTACT, BOOKING], {
    sourcesPillsLabel,
  });

  const pills = root.querySelectorAll(".sources-pill");
  Assert.equal(pills.length, 1, "The three sources share one summary pill");
  Assert.equal(root.querySelectorAll(".sources-pill-named").length, 0);
  Assert.deepEqual(
    [...(pills[0]?.querySelectorAll(".source-favicon") ?? [])].map(icon =>
      icon.getAttribute("src")
    ),
    [TRAVEL.favicon, CONTACT.favicon, BOOKING.favicon],
    "The summary pill shows all three source favicons"
  );
  Assert.equal(
    pills[0]?.querySelector(".sources-count:not(.sources-count-hover)")
      ?.textContent,
    sourcesPillsLabel,
    "The summary pill shows the supplied count label"
  );
});

add_task(async function test_named_pill_renders_a_long_source_name() {
  const label = "Long page title ".repeat(40) + "End of title";
  const root = await renderSources([{ ...TRAVEL, label }]);

  Assert.equal(
    root.querySelector(".sources-pill-named .source-label")?.textContent,
    label,
    "The named pill doesn't break the UI with a very long source name"
  );
});

/**
 * Updates the row's sources and waits for Lit to render them.
 *
 * @param {Array<{label: string, favicon: string}>} sources Sources to render.
 * @param {object} [labels] Test values for the row's preformatted labels.
 * @returns {Promise<ShadowRoot>} The rendered row's shadow root.
 */
async function renderSources(sources, labels = {}) {
  const { emptySourcesLabel, sourcesPillsLabel } = labels;
  Object.assign(row, { sources, emptySourcesLabel, sourcesPillsLabel });
  await row.updateComplete;
  return row.shadowRoot;
}
