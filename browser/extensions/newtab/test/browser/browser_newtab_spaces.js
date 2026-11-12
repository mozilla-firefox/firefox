"use strict";

/* global ContentTaskUtils */

const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);

const SPACES_OPT_OUT_PREFS = [
  "browser.newtabpage.activity-stream.spaces.storiesOptOut",
  "browser.newtabpage.activity-stream.spaces.widgetsOptOut",
  "browser.newtabpage.activity-stream.spaces.activityOptOut",
];

// PrefsFeed mirrors into these outside any pushed pref env, so they survive
// popPrefs. Clear them so each test starts as a profile that has not opted out.
function clearSpacesOptOuts() {
  SPACES_OPT_OUT_PREFS.forEach(pref => Services.prefs.clearUserPref(pref));
}

// Enrolls in a spaces trainhop config and waits for PrefsFeed to broadcast it.
async function enrollInSpaces(payload) {
  const doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "newtabTrainhop",
    value: { type: "multi-payload", payload },
  });
  await NimbusTestUtils.flushStore();
  return doExperimentCleanup;
}

/**
 * Tests the spaces page layout variants (Bug 2061742), which split the
 * .content-full-width band into separately-navigable panels. Which spaces exist
 * follows the content prefs, not the delivered content, so all three are present
 * here even though story data is not fetched in mochitest-browser.
 *
 * Test bodies run in the content process and cannot see helpers from this
 * scope, hence the repeated local probe helpers.
 */

function spacesPrefs(variant) {
  return [
    // @nova-cleanup(remove-pref): Drop this pref push; Nova is the only layout.
    ["browser.newtabpage.activity-stream.nova.enabled", true],
    ["browser.newtabpage.activity-stream.pageLayouts.variant", variant],
    ["browser.newtabpage.activity-stream.widgets.system.enabled", true],
    ["browser.newtabpage.activity-stream.widgets.enabled", true],
    ["browser.newtabpage.activity-stream.widgets.system.lists.enabled", true],
    ["browser.newtabpage.activity-stream.widgets.lists.enabled", true],
    ["browser.newtabpage.activity-stream.feeds.section.highlights", true],
    [
      "browser.newtabpage.activity-stream.discoverystream.config",
      JSON.stringify({ collapsible: true, enabled: true }),
    ],
  ];
}

test_newtab({
  async before({ pushPrefs }) {
    await pushPrefs(...spacesPrefs("spaces-buttons-bottom"));
  },

  test: async function test_spaces_navigation() {
    const container = await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector(".spaces-container"),
      "Wait for the spaces container to render"
    );

    const band = content.document.querySelector(".content-full-width");
    ok(band.classList.contains("spaces"), "The family class is on the band");
    ok(
      band.classList.contains("spaces-buttons-bottom"),
      "The placement class is on the band"
    );

    const tabs = Array.from(container.querySelectorAll('[role="tab"]'));
    is(tabs.length, 3, "One tab per populated space");
    is(
      tabs.map(tab => tab.id).join(","),
      "spaces-tab-stories,spaces-tab-widgets,spaces-tab-activity",
      "Tabs are in tablist order"
    );
    is(
      tabs[0].getAttribute("aria-selected"),
      "true",
      "The first space is selected"
    );
    is(tabs[1].tabIndex, -1, "The tablist is a single tab stop");

    const panels = Array.from(container.querySelectorAll('[role="tabpanel"]'));
    ok(!panels[0].hasAttribute("inert"), "The active space is interactive");
    ok(panels[1].hasAttribute("inert"), "The inactive space is not");
    ok(panels[2].hasAttribute("inert"), "Nor the one beyond it");

    // The property the impression observers depend on: an inactive space is
    // clipped away by the track, so its intersection rect is empty and its
    // contents never report as seen. Asserted geometrically because the story
    // feed that would report them is not populated here.
    const track = container.querySelector(".spaces-track");
    const trackRect = track.getBoundingClientRect();
    const inactiveRect = panels[1].getBoundingClientRect();
    ok(
      inactiveRect.left >= trackRect.right - 1 ||
        inactiveRect.right <= trackRect.left + 1,
      "The inactive space sits outside the track's clip rect " +
        `(track ${trackRect.left}-${trackRect.right}, ` +
        `space ${inactiveRect.left}-${inactiveRect.right})`
    );
    is(
      content.getComputedStyle(track).overflow,
      "clip",
      "The track clips rather than merely offsetting"
    );

    tabs[2].click();

    await ContentTaskUtils.waitForCondition(
      () =>
        content.document
          .querySelector("#spaces-tab-activity")
          .getAttribute("aria-selected") === "true",
      "Wait for the second space to become active"
    );

    const after = Array.from(
      content.document.querySelectorAll('[role="tabpanel"]')
    );
    ok(after[2].classList.contains("active"), "The last space is now active");
    ok(!after[2].hasAttribute("inert"), "and is interactive");
    ok(after[0].hasAttribute("inert"), "while the first is not");
  },
});

test_newtab({
  async before({ pushPrefs }) {
    // Highlights and stories off leaves widgets as the only populated space.
    await pushPrefs(
      ...spacesPrefs("spaces-buttons-bottom"),
      ["browser.newtabpage.activity-stream.feeds.section.highlights", false],
      ["browser.newtabpage.activity-stream.feeds.section.topstories", false],
      ["browser.newtabpage.activity-stream.feeds.system.topstories", false]
    );
  },

  test: async function test_spaces_falls_back_below_two_spaces() {
    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector(".content-full-width"),
      "Wait for the content band to render"
    );

    const band = content.document.querySelector(".content-full-width");
    ok(
      !band.classList.contains("spaces"),
      "The band does not claim the spaces layout with one space"
    );
    ok(
      !content.document.querySelector(".spaces-container"),
      "A carousel of one is not built"
    );
    ok(
      content.document.querySelector(".widgets-section-container"),
      "The widgets still render, in the ordinary stacked band"
    );
  },
});

test_newtab({
  async before({ pushPrefs }) {
    // Everything spaces needs except the variant, which stays at its default.
    await pushPrefs(...spacesPrefs("nova-full-width"));
  },

  test: async function test_spaces_leaves_the_default_layout_alone() {
    const band = await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector(".content-full-width"),
      "Wait for the content band to render"
    );

    is(
      Array.from(band.classList).filter(name => name.startsWith("spaces"))
        .length,
      0,
      "No spaces class reaches the band when the variant is not assigned"
    );
    ok(
      !content.document.querySelector(".spaces-container"),
      "No spaces container is built"
    );
    ok(
      !content.document.querySelector('[role="tablist"]'),
      "No tablist is added to the page"
    );
    for (const selector of [
      ".spaces-frame",
      ".spaces-track",
      ".spaces-tablist-slot",
    ]) {
      ok(
        !content.document.querySelector(selector),
        `No ${selector} is added to the page`
      );
    }
    // The band still stacks its sections as its own children, rather than
    // through a track.
    ok(
      content.document.querySelector(".widgets-section-container"),
      "Widgets render in the band"
    );
    ok(
      content.document.querySelector(".layout-content-column"),
      "The content column renders in the band"
    );
    ok(
      content.document.querySelector(".layout-highlights-column"),
      "Recent Activity renders in the band"
    );
  },
});

let spacesCleanup;

test_newtab({
  async before({ pushPrefs }) {
    clearSpacesOptOuts();
    // Stories off on the user branch: the state a default-branch write cannot
    // reach, and the reason the experiment overrides rather than writes.
    // Turned off before the variant is assigned, so the mirror does not read it
    // as a toggle made while enrolled.
    await pushPrefs(
      ["browser.newtabpage.activity-stream.feeds.section.topstories", false],
      ...spacesPrefs("spaces-buttons-bottom")
    );
    spacesCleanup = await enrollInSpaces([
      { type: "stories", payload: { enabled: true } },
    ]);
    // Enrollment fires no pref change, so nothing is mirrored and the value
    // the profile arrived with stands.
    ok(
      !Services.prefs.getBoolPref(
        "browser.newtabpage.activity-stream.spaces.storiesOptOut"
      ),
      "Enrolling does not opt the profile out of the Stories space"
    );
  },

  test: async function test_spaces_overrides_a_prior_opt_out() {
    const container = await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector(".spaces-container"),
      "Wait for the spaces container to render"
    );

    const labels = Array.from(
      container.querySelectorAll('[role="tab"]'),
      tab => tab.textContent
    );
    ok(
      labels.some(label => label.includes("Stories")),
      "The Stories space comes back for a user who had stories off"
    );

    // The tab is not enough: the override has to reach selectLayoutRender too,
    // or the space renders as an empty panel.
    ok(
      content.document.querySelector(".layout-content-column .ds-card-grid") ||
        content.document.querySelector(".layout-content-column section"),
      "The Stories space has content in it, not an empty panel"
    );
  },

  async after() {
    clearSpacesOptOuts();
    await spacesCleanup();
  },
});

test_newtab({
  async before({ pushPrefs }) {
    clearSpacesOptOuts();
    // Stories on at enrollment, so the snapshot records "not off" ...
    await pushPrefs(...spacesPrefs("spaces-buttons-bottom"));
    spacesCleanup = await enrollInSpaces([
      { type: "stories", payload: { enabled: true } },
    ]);
    // ... and turning it off afterwards is the user's own choice. A plain user
    // write, not a pushed pref: pushing the default value leaves no user value
    // for the snapshot guard to see.
    Services.prefs.setBoolPref(
      "browser.newtabpage.activity-stream.feeds.section.topstories",
      false
    );
  },

  test: async function test_spaces_respects_a_toggle_made_while_enrolled() {
    const container = await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector(".spaces-container"),
      "Wait for the spaces container to render"
    );

    const labels = Array.from(
      container.querySelectorAll('[role="tab"]'),
      tab => tab.textContent
    );
    ok(
      !labels.some(label => label.includes("Stories")),
      "A profile that had stories on at enrollment keeps its later opt-out"
    );
  },

  async after() {
    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.feeds.section.topstories"
    );
    clearSpacesOptOuts();
    await spacesCleanup();
  },
});
