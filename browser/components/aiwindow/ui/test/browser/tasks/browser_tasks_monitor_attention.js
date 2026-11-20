/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const PREF_MONITOR_ATTENTION = "browser.smartwindow.agent.monitorAttention";
const PREF_MONITOR_ANNOUNCEMENT =
  "browser.smartwindow.agent.monitorAnnouncement";

const { MonitorAttention } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/MonitorAttention.sys.mjs"
);

let win;

add_setup(async function () {
  await setupMonitorButton();
  win = await openSharedAIWindow();
});

describe("Monitor attention dot", () => {
  let otherWin;

  afterEach(async () => {
    await closeMonitorPanel(win);
    if (otherWin) {
      await BrowserTestUtils.closeWindow(otherWin);
      otherWin = null;
    }
    setAnnouncementRollout(false);
    AIWindowToolbarButtons.clearMonitorAttention();
    clearAttentionPrefs();
  });

  describe("when a monitor matches", () => {
    it("lights the button in every window", async () => {
      otherWin = await openAIWindow();

      notifyMatch("monitor-1");

      for (const w of [win, otherWin]) {
        Assert.ok(
          getMonitorButton(w).hasAttribute("monitor-attention"),
          "A match puts the dot on the button in every window"
        );
      }
      // The badge is empty, so it only renders because the attribute makes it
      // display: block. Without that the dot would be invisible.
      const badge = getMonitorButton(win).querySelector(".toolbarbutton-badge");
      Assert.equal(
        win.getComputedStyle(badge).display,
        "block",
        "The dot is rendered in the badge slot"
      );
    });

    // Deliberately not stubbing toggleMonitorPanel: clearing the dot is its
    // job, so a stub would make the assertions vacuous.
    it("clears in every window when the panel opens", async () => {
      otherWin = await openAIWindow();
      notifyMatch("monitor-1");

      await openMonitorPanel(win);

      for (const w of [win, otherWin]) {
        Assert.ok(
          !getMonitorButton(w).hasAttribute("monitor-attention"),
          "Opening the panel clears the dot in every window"
        );
      }
    });
  });

  // A failed check lights the same dot, but the panel is not told about it: it
  // says so on its own row rather than under "New matches".
  describe("when a monitor fails to check", () => {
    it("lights the button without offering the monitor as a match", () => {
      notifyRunFailed("monitor-1");

      Assert.ok(
        getMonitorButton(win).hasAttribute("monitor-attention"),
        "A failed check puts the dot on the button"
      );
      Assert.deepEqual(
        AIWindowToolbarButtons.monitorAttentionIds,
        [],
        "The failed monitor is not offered to the panel as a new match"
      );
    });

    it("clears when the panel opens", async () => {
      notifyRunFailed("monitor-1");

      await openMonitorPanel(win);

      Assert.ok(
        !getMonitorButton(win).hasAttribute("monitor-attention"),
        "Opening the panel clears the dot a failed check put there"
      );
    });
  });

  // A match the user never came back for stops being advertised, so the dot
  // does not outlive its lifetime.
  describe("when the only match is older than the dot lifetime", () => {
    beforeEach(() => {
      const eightDaysAgo = Date.now() - 8 * 24 * 60 * 60 * 1000;
      Services.prefs.setStringPref(
        PREF_MONITOR_ATTENTION,
        JSON.stringify([{ id: "monitor-stale", at: eightDaysAgo }])
      );
    });

    it("is not shown on a new window's button", async () => {
      otherWin = await openAIWindow();

      Assert.ok(
        !getMonitorButton(otherWin).hasAttribute("monitor-attention"),
        "A match older than the dot lifetime does not show the dot"
      );
      Assert.deepEqual(
        AIWindowToolbarButtons.monitorAttentionIds,
        [],
        "An expired match is not offered to the panel either"
      );
    });
  });

  // The panel needs to know which monitors matched, not just that something
  // did.
  describe("matched monitor ids", () => {
    it("are reported newest first", () => {
      // Back to back, so the two matches land in the same millisecond: the
      // order has to come from how they were stored, not from timestamps.
      notifyMatch("monitor-1");
      notifyMatch("monitor-2");

      Assert.deepEqual(
        AIWindowToolbarButtons.monitorAttentionIds,
        ["monitor-2", "monitor-1"],
        "Matched monitors are reported newest first"
      );
    });

    it("move a monitor that matches again to the front", () => {
      notifyMatch("monitor-1");
      notifyMatch("monitor-2");

      notifyMatch("monitor-1");

      Assert.deepEqual(
        AIWindowToolbarButtons.monitorAttentionIds,
        ["monitor-1", "monitor-2"],
        "Matching again moves the monitor to the front rather than duplicating it"
      );
    });

    it("clear the dot when they are taken", () => {
      notifyMatch("monitor-1");
      notifyMatch("monitor-2");

      const taken = AIWindowToolbarButtons.takeMonitorAttentionIds();

      Assert.deepEqual(
        taken,
        ["monitor-2", "monitor-1"],
        "Taking the ids returns what the panel should highlight, in order"
      );
      Assert.ok(
        !AIWindowToolbarButtons.hasMonitorAttention,
        "Taking the ids clears the dot in the same step"
      );
      Assert.deepEqual(
        AIWindowToolbarButtons.monitorAttentionIds,
        [],
        "A second open has nothing left to highlight"
      );
    });
  });

  // A corrupt pref must not take the button down with it.
  describe("with a corrupt attention pref", () => {
    it("reads unparseable state as no matches", () => {
      Services.prefs.setStringPref(PREF_MONITOR_ATTENTION, "not json");

      assertNoAttention("Unparseable");
    });

    it("reads the shape it held before it carried an order as no matches", () => {
      Services.prefs.setStringPref(
        PREF_MONITOR_ATTENTION,
        JSON.stringify({ "monitor-1": Date.now() })
      );

      assertNoAttention("A stale shape");
    });

    // Reading does not repair the pref, so the next match has to.
    it("replaces state it could not read on the next match", () => {
      Services.prefs.setStringPref(PREF_MONITOR_ATTENTION, "not json");

      notifyMatch("monitor-1");

      Assert.deepEqual(
        AIWindowToolbarButtons.monitorAttentionIds,
        ["monitor-1"],
        "The next match replaces state that could not be read"
      );
    });

    it("keeps the valid entries around a malformed one", () => {
      Services.prefs.setStringPref(
        PREF_MONITOR_ATTENTION,
        JSON.stringify([{ id: "monitor-1", at: Date.now() }, null, { at: 5 }])
      );

      Assert.deepEqual(
        AIWindowToolbarButtons.monitorAttentionIds,
        ["monitor-1"],
        "Malformed entries are skipped, valid ones survive"
      );
    });
  });

  // The other reason for the dot: announcing the feature as new. Nimbus turns
  // this on via the default branch, and it lights the same dot.
  describe("while the feature is announced", () => {
    beforeEach(() => {
      setAnnouncementRollout(true);
    });

    it("lights the button for the announcement alone", () => {
      Assert.ok(
        getMonitorButton(win).hasAttribute("monitor-attention"),
        "Announcing the feature shows the dot"
      );
      Assert.ok(
        AIWindowToolbarButtons.hasMonitorAnnouncement,
        "The dot is attributed to the announcement"
      );
      Assert.ok(
        !MonitorAttention.hasAttention,
        "No monitor matched or failed, so the announcement is the only reason"
      );
      Assert.deepEqual(
        AIWindowToolbarButtons.monitorAttentionIds,
        [],
        "The announcement is not a monitor, so there is nothing to highlight"
      );
    });

    it("clears when the panel opens", async () => {
      await openMonitorPanel(win);

      Assert.ok(
        !getMonitorButton(win).hasAttribute("monitor-attention"),
        "Opening the panel answers the announcement too"
      );
    });

    // The dismissal is on the user branch, which outranks the default branch
    // Nimbus writes, so re-applying the rollout must not bring the dot back.
    it("stays dismissed when the rollout re-applies", () => {
      AIWindowToolbarButtons.clearMonitorAttention();

      setAnnouncementRollout(true);

      Assert.ok(
        !AIWindowToolbarButtons.hasMonitorAnnouncement,
        "A dismissed announcement stays dismissed when the rollout re-applies"
      );
    });

    // Unenrolling restores the default branch, which is the only thing keeping
    // the announcement up. A match is separate state and has to survive that.
    it("ends with the rollout but leaves a match lit", () => {
      notifyMatch("monitor-1");

      setAnnouncementRollout(false);

      Assert.ok(
        !AIWindowToolbarButtons.hasMonitorAnnouncement,
        "Unenrolling ends the announcement"
      );
      Assert.ok(
        AIWindowToolbarButtons.hasMonitorAttention,
        "The match still shows the dot on its own"
      );
      Assert.deepEqual(
        AIWindowToolbarButtons.monitorAttentionIds,
        ["monitor-1"],
        "Ending the rollout does not disturb what the panel highlights"
      );
    });
  });

  // The button is usable long before any rollout starts, so opening the panel
  // that early must not mask the announcement that comes later.
  describe("before any announcement", () => {
    it("opening the panel dismisses nothing", () => {
      AIWindowToolbarButtons.clearMonitorAttention();

      Assert.ok(
        !Services.prefs.prefHasUserValue(PREF_MONITOR_ANNOUNCEMENT),
        "Opening the panel with nothing to announce dismisses nothing"
      );
    });

    it("a rollout that starts later still lights the dot", () => {
      AIWindowToolbarButtons.clearMonitorAttention();

      setAnnouncementRollout(true);

      Assert.ok(
        AIWindowToolbarButtons.hasMonitorAnnouncement,
        "A rollout that starts later still reaches the user"
      );
    });
  });
});

function clearAttentionPrefs() {
  Services.prefs.clearUserPref(PREF_MONITOR_ATTENTION);
  Services.prefs.clearUserPref(PREF_MONITOR_ANNOUNCEMENT);
}

/**
 * Nimbus sets the announcement on the default branch, so tests that stand in
 * for a rollout have to write the same branch the dismissal outranks.
 *
 * @param {boolean} enabled
 */
function setAnnouncementRollout(enabled) {
  Services.prefs
    .getDefaultBranch("")
    .setBoolPref(PREF_MONITOR_ANNOUNCEMENT, enabled);
}

/**
 * @param {string} label - Describes the corrupt state under test.
 */
function assertNoAttention(label) {
  Assert.deepEqual(
    AIWindowToolbarButtons.monitorAttentionIds,
    [],
    `${label} state reads as no matches`
  );
  Assert.ok(
    !AIWindowToolbarButtons.hasMonitorAttention,
    `${label} state does not light the dot`
  );
}
