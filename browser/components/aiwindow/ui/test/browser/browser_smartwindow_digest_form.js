/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { DigestManager } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/digests/DigestManager.sys.mjs"
);
const { DigestStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/digests/DigestStore.sys.mjs"
);
const { PlacesTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PlacesTestUtils.sys.mjs"
);
const { DIGEST_STATUS } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/digests/DigestConstants.sys.mjs"
);

const FORM_PAGE =
  "chrome://mochitests/content/browser/browser/components/aiwindow/ui/test/browser/test_smartwindow_digest_form_page.html";

const DIGEST_STORE_PATH = PathUtils.join(
  PathUtils.profileDir,
  "memories",
  "digests.json.lz4"
);

const TITLE = "Updates on nutrition";

async function resetDigestStore() {
  await DigestStore.testOnlyFlushAndResetCache();
  await IOUtils.remove(DIGEST_STORE_PATH, { ignoreAbsent: true });
}

function frequency(el) {
  return el.shadowRoot.querySelector(".field-row moz-select").value;
}

function suggestedDigest(overrides = {}) {
  return DigestManager.createDigest({
    memory_id: "memory-health",
    title: TITLE,
    description: "Nutrition research",
    domains: [],
    schedule: { type: "daily", hour: 9, minute: 0 },
    ...overrides,
  });
}

function $(el, selector) {
  return el.shadowRoot.querySelector(selector);
}

async function typeInto(win, field, text) {
  field.focus();
  EventUtils.sendString(text, win);
  await field.getRootNode().host.updateComplete;
}

function dropdown(el, name) {
  return $(el, `moz-select[data-l10n-id="smartwindow-digest-form-${name}"]`);
}

/**
 * Opens a moz-select's dropdown with the mouse, then clicks the option with
 * the given value.
 *
 * @param {Window} win - The window the form is in.
 * @param {Element} mozSelect - The moz-select to choose from.
 * @param {string} value - The value of the option to choose.
 */
async function chooseOption(win, mozSelect, value) {
  const select = mozSelect.shadowRoot.querySelector("select");
  const target = [...select.options].findIndex(option => option.value == value);
  select.scrollIntoView({ block: "center" });
  await new Promise(resolve => win.requestAnimationFrame(resolve));

  const shown = BrowserTestUtils.waitForSelectPopupShown(window);
  EventUtils.synthesizeMouseAtCenter(select, {}, win);
  const popup = await shown;

  const changed = BrowserTestUtils.waitForEvent(mozSelect, "change");
  const hidden = BrowserTestUtils.waitForPopupEvent(popup, "hidden");
  const item = popup.querySelectorAll("menuitem")[target];
  item.scrollIntoView({ block: "nearest" });
  EventUtils.synthesizeMouseAtCenter(item, {}, window);
  await Promise.all([changed, hidden]);
  await mozSelect.getRootNode().host.updateComplete;
}

async function clickSubmit(win, el) {
  const button = $(el, ".submit-button");
  await button.updateComplete;
  button.scrollIntoView({ block: "center" });
  await TestUtils.waitForCondition(
    () => button.getBoundingClientRect().width,
    "The submit button has rendered"
  );
  await new Promise(resolve => win.requestAnimationFrame(resolve));
  EventUtils.synthesizeMouseAtCenter(button, {}, win);
}

add_setup(async function () {
  // macOS opens select dropdowns as native menus, which synthesized keys
  // can't drive.
  await SpecialPowers.pushPrefEnv({
    set: [["widget.macos.allow-native-select", false]],
  });
});

describe("smartwindow-digest-form", () => {
  let tab, win, el;

  /**
   * Opens the test page with a digest form on it, set up the way a host sets
   * it up before showing it.
   *
   * @param {object} props - Properties to set on the form before it renders.
   */
  async function openForm(props) {
    tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, FORM_PAGE);
    win = tab.linkedBrowser.contentWindow;
    await win.customElements.whenDefined("smartwindow-digest-form");
    el = win.document.createElement("smartwindow-digest-form");
    Object.assign(el, props);
    win.document.body.appendChild(el);
    await el.updateComplete;
  }

  beforeEach(async () => {
    await resetDigestStore();
  });

  afterEach(async () => {
    if (tab) {
      BrowserTestUtils.removeTab(tab);
    }
    tab = win = el = null;
    await resetDigestStore();
  });

  describe("creating", () => {
    it("starts on a daily schedule whatever the suggestion was saved with", async () => {
      await openForm({
        digest: await suggestedDigest({
          schedule: { type: "weekly", weekday: 3, hour: 18, minute: 0 },
        }),
      });
      is(frequency(el), "daily", "The frequency starts on daily");
    });

    it("starts with an empty description", async () => {
      await openForm({ digest: await suggestedDigest() });

      is(
        $(el, ".description-input").value,
        "",
        "The description starts empty even though the suggestion has one"
      );
    });
  });

  describe("editing", () => {
    it("shows the saved schedule", async () => {
      const digest = await DigestManager.enableDigest(
        (
          await suggestedDigest({
            schedule: { type: "weekly", weekday: 3, hour: 18, minute: 0 },
          })
        ).id
      );
      await openForm({ mode: "edit", digest });
      is(frequency(el), "weekly", "The frequency is the saved one");
    });

    it("shows the saved description", async () => {
      const digest = await DigestManager.enableDigest(
        (await suggestedDigest()).id
      );
      await openForm({ mode: "edit", digest });

      is(
        $(el, ".description-input").value,
        digest.description,
        "The description shows the saved one"
      );
    });
  });

  describe("description", () => {
    it("is required before the form saves", async () => {
      const digest = await suggestedDigest();
      await openForm({ digest });
      let fired = false;
      const onSaveEvent = () => (fired = true);
      el.addEventListener("digest-form:save-success", onSaveEvent);
      el.addEventListener("digest-form:save-error", onSaveEvent);

      await clickSubmit(win, el);
      const description = $(el, ".description-input");
      await TestUtils.waitForCondition(
        () => el.shadowRoot.activeElement == description,
        "Focus moves to the description"
      );

      ok(
        description.hasAttribute("invalid"),
        "The description is marked invalid"
      );
      ok(
        $(el, ".field-description .error-message"),
        "An error message is shown in the description field"
      );
      ok(!fired, "No save event fires for an invalid form");
      is(
        (await DigestManager.getDigestById(digest.id)).status,
        DIGEST_STATUS.SUGGESTED,
        "The digest isn't turned on"
      );

      await typeInto(win, description, "Food news");
      ok(!description.hasAttribute("invalid"), "Typing clears the error");
    });
  });

  describe("sources", () => {
    const chips = () => [
      ...el.shadowRoot.querySelectorAll("smartwindow-chip[removable]"),
    ];

    it("adds a site as a chip on Enter and rejects invalid ones", async () => {
      await openForm({ digest: await suggestedDigest() });
      const sources = $(el, ".sources-input");

      await typeInto(win, sources, "https://www.bbcnews.com/health");
      EventUtils.synthesizeKey("KEY_Enter", {}, win);
      await el.updateComplete;
      Assert.deepEqual(
        chips().map(chip => chip.label),
        ["bbcnews.com"],
        "Enter adds the site as a chip"
      );

      await typeInto(win, sources, "not a site");
      EventUtils.synthesizeKey("KEY_Enter", {}, win);
      await el.updateComplete;
      ok(sources.hasAttribute("invalid"), "An invalid address is rejected");
      ok(
        $(el, ".field-sources .error-message"),
        "An error message is shown in the sources field"
      );
      is(chips().length, 1, "No chip is added for the invalid address");
    });

    it("rejects addresses that aren't websites", async () => {
      await openForm({ digest: await suggestedDigest() });
      const sources = $(el, ".sources-input");

      for (const address of [
        "notasite",
        "localhost",
        "192.168.0.1",
        "example.com:8080",
      ]) {
        await typeInto(win, sources, address);
        EventUtils.synthesizeKey("KEY_Enter", {}, win);
        await el.updateComplete;
        ok(sources.hasAttribute("invalid"), `${address} is rejected`);
        is(chips().length, 0, `No chip is added for ${address}`);

        sources.focus();
        EventUtils.synthesizeKey("a", { accelKey: true }, win);
        EventUtils.synthesizeKey("KEY_Backspace", {}, win);
        await el.updateComplete;
      }

      await typeInto(win, sources, "bbc.co.uk");
      EventUtils.synthesizeKey("KEY_Enter", {}, win);
      await el.updateComplete;
      Assert.deepEqual(
        chips().map(chip => chip.label),
        ["bbc.co.uk"],
        "A country-code domain is accepted"
      );
    });

    describe("favicons", () => {
      afterEach(async () => {
        await PlacesTestUtils.clearFavicons();
        await PlacesUtils.history.clear();
      });

      it("uses the www host's icon when the bare host has none", async () => {
        const ICON =
          "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8DwHwAFBQIAX8jx0gAAAABJRU5ErkJggg==";
        await PlacesTestUtils.addVisits("https://www.example.com/");
        await PlacesTestUtils.addFavicons(
          new Map([["https://www.example.com/", ICON]])
        );
        await openForm({ digest: await suggestedDigest() });
        const sources = $(el, ".sources-input");

        for (const site of ["example.com", "example.org"]) {
          await typeInto(win, sources, site);
          EventUtils.synthesizeKey("KEY_Enter", {}, win);
          await el.updateComplete;
        }

        await TestUtils.waitForCondition(
          () => chips()[0].iconSrc === "page-icon:https://www.example.com/",
          "A site visited only on www shows the www site's icon"
        );
        is(
          chips()[1].iconSrc,
          "page-icon:https://example.org/",
          "A site with no saved icon keeps its own address"
        );
      });
    });
  });

  describe("memory-tooltip", () => {
    let settingsTab;

    afterEach(() => {
      if (settingsTab) {
        BrowserTestUtils.removeTab(settingsTab);
      }
      settingsTab = null;
    });

    it("opens on click, closes on Escape and links to memory settings", async () => {
      await openForm({ digest: await suggestedDigest() });
      const button = $(el, ".memory-tooltip-trigger");
      const tooltip = $(el, ".memory-tooltip");
      await TestUtils.waitForCondition(
        () => button.getBoundingClientRect().width,
        "The memory-tooltip trigger has rendered"
      );

      EventUtils.synthesizeMouseAtCenter(button, {}, win);
      await TestUtils.waitForCondition(
        () => tooltip.matches(":popover-open"),
        "The trigger opens the memory-tooltip"
      );
      is(
        button.ariaExpanded,
        "true",
        "The trigger reports the memory-tooltip as open"
      );

      EventUtils.synthesizeKey("KEY_Escape", {}, win);
      await TestUtils.waitForCondition(
        () => !tooltip.matches(":popover-open"),
        "Escape closes the memory-tooltip"
      );

      EventUtils.synthesizeMouseAtCenter(button, {}, win);
      await TestUtils.waitForCondition(
        () => tooltip.matches(":popover-open"),
        "The memory-tooltip opens again"
      );
      const link = tooltip.querySelector("a");
      await TestUtils.waitForCondition(
        () => link.getBoundingClientRect().width,
        "The memories link has rendered"
      );
      const newTab = BrowserTestUtils.waitForNewTab(
        gBrowser,
        url =>
          url.startsWith("about:preferences") ||
          url.startsWith("about:settings"),
        true
      );
      EventUtils.synthesizeMouseAtCenter(link, {}, win);
      settingsTab = await newTab;
      ok(
        settingsTab.linkedBrowser.currentURI.spec.includes("manageMemories"),
        "The link opens the memory settings"
      );
      ok(
        !tooltip.matches(":popover-open"),
        "Following the link closes the memory-tooltip"
      );
    });
  });

  describe("saving", () => {
    it("turns on a suggested digest when submitted in default mode", async () => {
      const suggested = await suggestedDigest();
      await openForm({ digest: suggested });
      const description = "Dietary guidance";
      await typeInto(win, $(el, ".description-input"), description);

      const saved = BrowserTestUtils.waitForEvent(
        el,
        "digest-form:save-success"
      );
      await clickSubmit(win, el);
      const { detail } = await saved;

      is(detail.mode, "default", "The event reports the mode");
      const stored = await DigestManager.getDigestById(suggested.id);
      is(stored.status, DIGEST_STATUS.ENABLED, "The stored digest is on");
      is(stored.description, description, "The description is saved");
    });

    it("saves edits, including a site left in the input", async () => {
      const existing = await DigestManager.enableDigest(
        (await suggestedDigest()).id
      );
      await openForm({ mode: "edit", digest: existing });
      await typeInto(win, $(el, ".sources-input"), "apnews.com");

      const saved = BrowserTestUtils.waitForEvent(
        el,
        "digest-form:save-success"
      );
      await clickSubmit(win, el);
      const { detail } = await saved;

      is(detail.mode, "edit", "The event reports the mode");
      const stored = await DigestManager.getDigestById(existing.id);
      Assert.deepEqual(
        stored.domains,
        ["apnews.com"],
        "A site left in the input is saved on submit"
      );
      is(stored.status, DIGEST_STATUS.ENABLED, "The digest stays on");
    });

    it("saves the frequency, day and time chosen from the dropdowns", async () => {
      const existing = await DigestManager.enableDigest(
        (await suggestedDigest()).id
      );
      await openForm({ mode: "edit", digest: existing });
      ok(!dropdown(el, "day"), "A daily digest has no day dropdown");

      await chooseOption(win, dropdown(el, "frequency"), "weekly");
      ok(dropdown(el, "day"), "Choosing weekly shows the day dropdown");
      await chooseOption(win, dropdown(el, "day"), "5");
      await chooseOption(win, dropdown(el, "time"), "18");

      const saved = BrowserTestUtils.waitForEvent(
        el,
        "digest-form:save-success"
      );
      await clickSubmit(win, el);
      await saved;

      Assert.deepEqual(
        (await DigestManager.getDigestById(existing.id)).schedule,
        { type: "weekly", weekday: 5, hour: 18, minute: 0 },
        "The stored schedule is the one chosen"
      );
    });

    it("saves an on-demand digest without a time", async () => {
      const existing = await DigestManager.enableDigest(
        (await suggestedDigest()).id
      );
      await openForm({ mode: "edit", digest: existing });

      await chooseOption(win, dropdown(el, "frequency"), "on-demand");
      ok(!dropdown(el, "time"), "Choosing on demand hides the time dropdown");

      const saved = BrowserTestUtils.waitForEvent(
        el,
        "digest-form:save-success"
      );
      await clickSubmit(win, el);
      await saved;

      Assert.deepEqual(
        (await DigestManager.getDigestById(existing.id)).schedule,
        { type: "on-demand" },
        "The stored schedule has no time"
      );
    });

    it("fires save-error when the digest no longer exists", async () => {
      const existing = await DigestManager.enableDigest(
        (await suggestedDigest()).id
      );
      await openForm({ mode: "edit", digest: existing });
      await DigestManager.deleteDigest(existing.id);

      const failed = BrowserTestUtils.waitForEvent(
        el,
        "digest-form:save-error"
      );
      await clickSubmit(win, el);
      const { detail } = await failed;

      is(detail.mode, "edit", "The event reports the mode");
      ok(detail.error, "The event carries the error");
      ok(!$(el, ".submit-button").disabled, "The form can be submitted again");
    });
  });
});
