/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TOAST_PAGE =
  "chrome://mochitests/content/browser/browser/components/aiwindow/ui/test/browser/test_smartwindow_digest_toast_page.html";

const TITLE = "Nutrition research";

function makeDigest(type) {
  return {
    id: `digest-${type}`,
    title: TITLE,
    schedule: { type },
  };
}

describe("smartwindow-digest-toast", () => {
  let tab, win;

  const messageBar = toast => toast.shadowRoot.querySelector("moz-message-bar");

  async function click(target) {
    await TestUtils.waitForCondition(
      () => target.getBoundingClientRect().width,
      "The target has rendered"
    );
    EventUtils.synthesizeMouseAtCenter(target, {}, win);
  }

  // Adds a toast to the page and resolves once it shows with its text.
  async function addToast(properties) {
    const toast = win.document.createElement("smartwindow-digest-toast");
    Object.assign(toast, properties);
    win.document.body.appendChild(toast);
    await TestUtils.waitForCondition(
      () => toast.matches(":popover-open") && messageBar(toast)?.heading,
      "The toast shows with its text"
    );
    return toast;
  }

  const show = digest => addToast({ type: "success", digest });
  const showError = mode => addToast({ type: "error", mode });

  beforeEach(async () => {
    tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, TOAST_PAGE);
    win = tab.linkedBrowser.contentWindow;
    await win.customElements.whenDefined("smartwindow-digest-toast");
  });

  afterEach(() => {
    BrowserTestUtils.removeTab(tab);
    tab = win = null;
  });

  describe("showing", () => {
    it("names the schedule for daily and weekly digests", async () => {
      for (const [type, l10nId] of [
        ["daily", "smartwindow-digest-toast-created-daily"],
        ["weekly", "smartwindow-digest-toast-created-weekly"],
        ["on-demand", "smartwindow-digest-toast-created"],
      ]) {
        const bar = messageBar(await show(makeDigest(type)));
        is(bar.dataset.l10nId, l10nId, `Uses ${l10nId} for ${type}`);
        ok(bar.heading.includes(TITLE), "The heading names the digest");
        ok(bar.message, "The toast has a message");
      }
    });

    it("pins to the top corner of the page without taking focus", async () => {
      const input = win.document.createElement("input");
      input.setAttribute("aria-label", "Search");
      win.document.body.appendChild(input);
      await click(input);
      is(win.document.activeElement, input, "The input has focus");
      const toast = await show(makeDigest("daily"));
      is(win.document.activeElement, input, "Focus did not move");

      const rect = toast.getBoundingClientRect();
      Assert.less(rect.top, 50, "The toast is at the top of the page");
      Assert.less(
        win.innerWidth - rect.right,
        50,
        "The toast is at the inline end of the page"
      );
      Assert.less(
        rect.width,
        win.innerWidth / 2,
        "The toast does not span the page"
      );
    });
  });

  describe("closing", () => {
    let toast;

    beforeEach(async () => {
      toast = await show(makeDigest("daily"));
    });

    it("removes itself when its close button is clicked", async () => {
      await click(
        messageBar(toast).shadowRoot.querySelector("moz-button.close")
      );
      await TestUtils.waitForCondition(
        () => !toast.isConnected,
        "The toast is removed"
      );
    });

    it("dispatches the view event and closes when View digest is clicked", async () => {
      const viewed = BrowserTestUtils.waitForEvent(
        win.document,
        "smartwindow-digest-view"
      );
      await click(messageBar(toast).querySelector(".view-button"));
      const { detail } = await viewed;

      is(detail.digest.id, "digest-daily", "The event carries the digest");
      await TestUtils.waitForCondition(
        () => !toast.isConnected,
        "The toast is removed"
      );
    });
  });

  describe("multiple toasts", () => {
    // The toast drawn at the toast's center, which is the one on top.
    function topToastAt(toast) {
      const rect = toast.getBoundingClientRect();
      return win.document.elementFromPoint(
        rect.left + rect.width / 2,
        rect.top + rect.height / 2
      );
    }

    it("opens a new toast on top of the earlier one", async () => {
      const first = await show(makeDigest("daily"));
      const second = await showError("default");
      ok(first.matches(":popover-open"), "The earlier toast stays open");
      is(topToastAt(first), second, "The new toast is on top");
      is(
        messageBar(first).dataset.l10nId,
        "smartwindow-digest-toast-created-daily",
        "The earlier toast keeps its own content"
      );
    });

    it("shows the earlier toast again when the top one closes", async () => {
      const first = await show(makeDigest("daily"));
      const second = await showError("default");
      await click(
        messageBar(second).shadowRoot.querySelector("moz-button.close")
      );
      await TestUtils.waitForCondition(
        () => !second.isConnected,
        "The top toast is removed"
      );
      is(topToastAt(first), first, "The earlier toast is on top again");
    });
  });

  describe("errors", () => {
    it("says a digest couldn't be created", async () => {
      const bar = messageBar(await showError("default"));
      is(bar.type, "error", "The toast is an error");
      is(
        bar.dataset.l10nId,
        "smartwindow-digest-toast-error-create",
        "The toast is for a failed create"
      );
      ok(bar.message, "The toast has a message");
      ok(!bar.querySelector(".view-button"), "There is no View digest button");
    });

    it("says changes couldn't be saved when editing", async () => {
      const bar = messageBar(await showError("edit"));
      is(
        bar.dataset.l10nId,
        "smartwindow-digest-toast-error-edit",
        "The toast is for a failed edit"
      );
    });
  });

  describe("switching to classic", () => {
    beforeEach(() => {
      AIWindow.toggleAIWindow(window, true);
    });

    afterEach(() => {
      AIWindow.toggleAIWindow(window, false);
    });

    it("removes itself when the window switches to classic", async () => {
      const toast = await show(makeDigest("daily"));
      AIWindow.toggleAIWindow(window, false);
      ok(!toast.isConnected, "The toast is removed");
    });
  });

  describe("announcing", () => {
    // Held so the accessibility service keeps running during the test.
    let accService;

    // Wait for the page's accessibility tree to finish building before showing toasts.
    beforeEach(async () => {
      accService = Cc["@mozilla.org/accessibilityService;1"].getService(
        Ci.nsIAccessibilityService
      );
      await TestUtils.waitForCondition(() => {
        const docAccessible = accService.getAccessibleFor(win.document);
        if (!docAccessible) {
          return false;
        }
        const state = {};
        docAccessible.getState(state, {});
        return !(state.value & Ci.nsIAccessibleStates.STATE_BUSY);
      }, "The page's accessibility tree is built");
    });

    afterEach(() => {
      accService = null;
    });

    // Resolves with the heading of the next toast that is announced.
    function waitForAlert() {
      return new Promise(resolve => {
        const observer = subject => {
          const event = subject.QueryInterface(Ci.nsIAccessibleEvent);
          const node = event.DOMNode;
          if (
            event.eventType == Ci.nsIAccessibleEvent.EVENT_ALERT &&
            node?.getRootNode().host?.localName == "smartwindow-digest-toast"
          ) {
            Services.obs.removeObserver(observer, "accessible-event");
            resolve(node.heading);
          }
        };
        Services.obs.addObserver(observer, "accessible-event");
      });
    }

    it("announces a toast with its text", async () => {
      const announced = waitForAlert();
      await show(makeDigest("daily"));
      ok((await announced)?.includes(TITLE), "The toast is announced");
    });

    it("announces a toast added while another is showing", async () => {
      const firstAnnounced = waitForAlert();
      await show(makeDigest("daily"));
      await firstAnnounced;

      const announced = waitForAlert();
      const second = await showError("default");
      is(
        await announced,
        messageBar(second).heading,
        "The new toast is announced with its text"
      );
    });
  });
});
