/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { DigestManager } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/digests/DigestManager.sys.mjs"
);
const { DigestStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/digests/DigestStore.sys.mjs"
);
const { DIGEST_STATUS } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/digests/DigestConstants.sys.mjs"
);

const MODAL_PAGE =
  "chrome://mochitests/content/browser/browser/components/aiwindow/ui/test/browser/test_smartwindow_digest_modal_page.html";

const DIGEST_STORE_PATH = PathUtils.join(
  PathUtils.profileDir,
  "memories",
  "digests.json.lz4"
);

async function resetDigestStore() {
  await DigestStore.testOnlyFlushAndResetCache();
  await IOUtils.remove(DIGEST_STORE_PATH, { ignoreAbsent: true });
}

function suggestedDigest() {
  return DigestManager.createDigest({
    memory_id: "memory-health",
    title: "Updates on nutrition",
    description: "Nutrition research",
    domains: [],
    schedule: { type: "daily", hour: 9, minute: 0 },
  });
}

describe("smartwindow-digest-modal", () => {
  let tab, win, modal;

  const dialog = () => modal.shadowRoot.querySelector("dialog");
  const form = () => modal.shadowRoot.querySelector("smartwindow-digest-form");
  const inForm = selector => form().shadowRoot.querySelector(selector);

  async function openModal(options) {
    await modal.open(options);
    await form().updateComplete;
  }

  async function click(target) {
    target.scrollIntoView({ block: "center" });
    await TestUtils.waitForCondition(
      () => target.getBoundingClientRect().width,
      "The target has rendered"
    );
    EventUtils.synthesizeMouseAtCenter(target, {}, win);
  }

  async function typeInto(field, text) {
    field.focus();
    EventUtils.sendString(text, win);
    await form().updateComplete;
  }

  beforeEach(async () => {
    await resetDigestStore();
    tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, MODAL_PAGE);
    win = tab.linkedBrowser.contentWindow;
    await win.customElements.whenDefined("smartwindow-digest-modal");
    modal = win.document.createElement("smartwindow-digest-modal");
    win.document.body.appendChild(modal);
    await modal.updateComplete;
  });

  afterEach(async () => {
    BrowserTestUtils.removeTab(tab);
    tab = win = modal = null;
    await resetDigestStore();
  });

  describe("opening", () => {
    it("shows the form for the digest in a modal dialog", async () => {
      const digest = await suggestedDigest();
      ok(!form(), "No form is rendered while the modal is closed");

      await openModal({ digest });
      ok(dialog().matches(":modal"), "The dialog is open as a modal");
      is(form().digest, digest, "The form shows the digest it was opened with");
      is(form().mode, "default", "The form opens in default mode");
    });

    it("opens the form in edit mode when asked", async () => {
      const digest = await DigestManager.enableDigest(
        (await suggestedDigest()).id
      );
      await openModal({ digest, mode: "edit" });
      is(form().mode, "edit", "The form opens in edit mode");
      is(
        inForm(".description-input").value,
        digest.description,
        "The form shows the saved description"
      );
    });

    it("starts from the digest again after being closed", async () => {
      const digest = await suggestedDigest();
      await openModal({ digest });
      await typeInto(inForm(".description-input"), "Unsaved edits");
      modal.close();

      await openModal({ digest });
      is(
        inForm(".description-input").value,
        "",
        "Unsaved edits are gone when the modal opens again"
      );
    });
  });

  describe("closing", () => {
    beforeEach(async () => {
      await openModal({ digest: await suggestedDigest() });
    });

    it("closes on the close button", async () => {
      await click(modal.shadowRoot.querySelector(".close-button"));
      await TestUtils.waitForCondition(
        () => !dialog().open,
        "The close button closes the modal"
      );
      ok(!form(), "The form is removed");
    });

    it("closes on Escape", async () => {
      EventUtils.synthesizeKey("KEY_Escape", {}, win);
      await TestUtils.waitForCondition(
        () => !dialog().open,
        "Escape closes the modal"
      );
    });

    it("closes on the form's cancel button", async () => {
      await click(inForm("moz-button-group moz-button:not(.submit-button)"));
      await TestUtils.waitForCondition(
        () => !dialog().open,
        "Cancel closes the modal"
      );
    });
  });

  describe("saving", () => {
    it("closes after a successful save and lets the event reach the page", async () => {
      const digest = await suggestedDigest();
      await openModal({ digest });
      await typeInto(inForm(".description-input"), "Dietary guidance");

      const saved = BrowserTestUtils.waitForEvent(
        win.document,
        "digest-form:save-success"
      );
      await click(inForm(".submit-button"));
      await saved;

      await TestUtils.waitForCondition(
        () => !dialog().open,
        "The modal closes after saving"
      );
      is(
        (await DigestManager.getDigestById(digest.id)).status,
        DIGEST_STATUS.ENABLED,
        "The digest is turned on"
      );
    });

    it("closes after a failed save and lets the event reach the page", async () => {
      const digest = await DigestManager.enableDigest(
        (await suggestedDigest()).id
      );
      await openModal({ digest, mode: "edit" });
      await DigestManager.deleteDigest(digest.id);

      const failed = BrowserTestUtils.waitForEvent(
        win.document,
        "digest-form:save-error"
      );
      await click(inForm(".submit-button"));
      await failed;

      await TestUtils.waitForCondition(
        () => !dialog().open,
        "The modal closes after a failed save"
      );
    });
  });
});
