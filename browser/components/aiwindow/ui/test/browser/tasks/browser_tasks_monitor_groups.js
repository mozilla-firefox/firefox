/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_URL =
  getRootDirectory(gTestPath) + "test_monitors_display_page.html";
const ACTIVE_LIMIT = 15;

/**
 * Gives the page's monitors-display the given number of active and paused
 * monitors.
 *
 * @param {MozBrowser} browser - The test page's browser
 * @param {number} activeCount - Number of active monitors
 * @param {number} pausedCount - Number of paused monitors
 */
async function setMonitors(browser, activeCount, pausedCount) {
  await SpecialPowers.spawn(
    browser,
    [activeCount, pausedCount, ACTIVE_LIMIT],
    async (active, paused, limit) => {
      await content.customElements.whenDefined("monitors-display");
      const display = content.document.getElementById("test-monitors");
      const makeMonitor = (id, enabled) => ({
        id,
        title: `Monitor ${id}`,
        enabled,
        watchUrls: ["https://example.com/"],
        history: [],
      });
      display.activeLimit = limit;
      display.monitors = [
        ...Array.from({ length: active }, (_, i) =>
          makeMonitor(`active-${i}`, true)
        ),
        ...Array.from({ length: paused }, (_, i) =>
          makeMonitor(`paused-${i}`, false)
        ),
      ];
      await display.updateComplete;
      for (const group of display.shadowRoot.querySelectorAll(
        "ai-tasks-group"
      )) {
        await group.updateComplete;
      }
    }
  );
}

describe("when the Tasks page lists monitors", () => {
  let gTab;

  beforeEach(async () => {
    gTab = await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_URL);
  });

  afterEach(() => {
    BrowserTestUtils.removeTab(gTab);
  });

  describe("with active and paused monitors", () => {
    beforeEach(async () => {
      await setMonitors(gTab.linkedBrowser, 7, 2);
    });

    it("should show an Active group and a Paused group with their counts", async () => {
      await SpecialPowers.spawn(gTab.linkedBrowser, [], () => {
        const display = content.document.getElementById("test-monitors");
        const [active, paused, ...rest] =
          display.shadowRoot.querySelectorAll("ai-tasks-group");

        Assert.equal(rest.length, 0, "There are only two groups");
        Assert.equal(
          active.headingL10nId,
          "ai-tasks-group-monitors-active",
          "The first group is the Active group"
        );
        Assert.deepEqual(
          active.countL10nArgs,
          { count: 7, limit: 15 },
          "The Active count shows active monitors out of the limit"
        );
        Assert.equal(
          paused.headingL10nId,
          "ai-tasks-group-monitors-paused",
          "The second group is the Paused group"
        );
        Assert.deepEqual(
          paused.countL10nArgs,
          { count: 2 },
          "The Paused count shows paused monitors"
        );
      });
    });

    it("should show only the first five monitors in a group", async () => {
      await SpecialPowers.spawn(gTab.linkedBrowser, [], () => {
        const display = content.document.getElementById("test-monitors");
        const [active, paused] =
          display.shadowRoot.querySelectorAll("ai-tasks-group");
        const visibleCards = group =>
          [...group.querySelectorAll("agent-monitor-item")].filter(
            card => content.getComputedStyle(card).display != "none"
          ).length;

        Assert.equal(visibleCards(active), 5, "Five active monitors show");
        Assert.equal(visibleCards(paused), 2, "Both paused monitors show");
        Assert.ok(
          !paused.shadowRoot.querySelector(".toggle-button"),
          "A group of five or fewer has no Show more button"
        );
      });
    });

    it("should show every monitor after Show more, and five after Show less", async () => {
      await SpecialPowers.spawn(gTab.linkedBrowser, [], async () => {
        const display = content.document.getElementById("test-monitors");
        const active = display.shadowRoot.querySelector("ai-tasks-group");
        const visibleCards = () =>
          [...active.querySelectorAll("agent-monitor-item")].filter(
            card => content.getComputedStyle(card).display != "none"
          ).length;
        const toggle = active.shadowRoot.querySelector(".toggle-button");

        Assert.equal(
          toggle.dataset.l10nId,
          "ai-tasks-group-show-more",
          "The button offers to show more"
        );
        Assert.deepEqual(
          JSON.parse(toggle.dataset.l10nArgs),
          { count: 2 },
          "The button counts the hidden monitors"
        );
        Assert.equal(
          toggle.getAttribute("aria-expanded"),
          "false",
          "The button reports the group as collapsed"
        );

        toggle.click();
        await active.updateComplete;
        Assert.equal(visibleCards(), 7, "Every active monitor shows");
        Assert.equal(
          toggle.dataset.l10nId,
          "ai-tasks-group-show-less",
          "The button offers to show less"
        );
        Assert.equal(
          toggle.getAttribute("aria-expanded"),
          "true",
          "The button reports the group as expanded"
        );

        toggle.click();
        await ContentTaskUtils.waitForCondition(
          () => !active.expanded,
          "The group collapses once the hiding animation finishes"
        );
        await active.updateComplete;
        Assert.equal(visibleCards(), 5, "Five active monitors show again");
      });
    });
  });

  describe("with only active monitors", () => {
    beforeEach(async () => {
      await setMonitors(gTab.linkedBrowser, 1, 0);
    });

    it("should not show a Paused group", async () => {
      await SpecialPowers.spawn(gTab.linkedBrowser, [], () => {
        const display = content.document.getElementById("test-monitors");
        const groups = display.shadowRoot.querySelectorAll("ai-tasks-group");

        Assert.equal(groups.length, 1, "Only one group shows");
        Assert.equal(
          groups[0].headingL10nId,
          "ai-tasks-group-monitors-active",
          "The group that shows is the Active group"
        );
      });
    });
  });
});
