/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MonitorAgent } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/agents/MonitorAgent.sys.mjs"
);

const { SpecialMessageActions } = ChromeUtils.importESModule(
  "resource://messaging-system/lib/SpecialMessageActions.sys.mjs"
);

const TITLED_PAGE_URL =
  "https://example.com/browser/browser/components/aiwindow/ui/test/browser/test_context_url_page.html";

let win;

add_setup(async function () {
  await setupMonitorButton();
  win = await openSharedAIWindow();
});

describe("Monitor panel create view", () => {
  let sandbox;

  beforeEach(async () => {
    sandbox = sinon.createSandbox();
    sandbox.stub(MonitorAgent, "listMonitors").resolves([]);
    await promiseNavigateAndLoad(
      win.gBrowser.selectedBrowser,
      "https://example.com/"
    );
  });

  afterEach(async () => {
    await closeMonitorPanel(win);
    sandbox.restore();
  });

  it("opens from the Create new task row", async () => {
    const { contents } = await openMonitorPanel(win);

    await openCreateView(contents);

    Assert.equal(contents.view, "create", "Panel switches to the create view");
    const form = contents.shadowRoot.querySelector("agent-monitor-item");
    Assert.equal(form?.mode, "create", "Monitor item form is in create mode");
    Assert.ok(
      !form.selfContained,
      "The panel frames and titles the form itself"
    );
  });

  it("creates a monitor from the submitted form and returns to the list", async () => {
    const createMonitor = sandbox
      .stub(MonitorAgent, "createMonitor")
      .resolves("id-1");
    const { contents } = await openMonitorPanel(win);
    await openCreateView(contents);

    contents.shadowRoot.querySelector("agent-monitor-item").dispatchEvent(
      new win.CustomEvent("agent-monitor-item:submit", {
        detail: {
          mode: "create",
          monitorName: "Ticket price",
          condition: "price drops",
          watchUrls: ["https://example.com/"],
          schedule: { frequency: "weekly", time: "09:30", weekday: 3 },
        },
        bubbles: true,
        composed: true,
      })
    );

    await TestUtils.waitForCondition(() => contents.view === "list");
    Assert.ok(createMonitor.calledOnce, "Submitting creates one monitor");
    Assert.deepEqual(
      createMonitor.firstCall.args[0],
      {
        prompt: "price drops",
        watchUrls: ["https://example.com/"],
        pageTitle: "Ticket price",
        schedule: { type: "weekly", hour: 9, minute: 30, weekday: 3 },
        source: "toolbar_panel",
      },
      "Form values are mapped to the shape MonitorAgent stores"
    );
    Assert.equal(
      contents.justCreatedId,
      "id-1",
      "The new monitor is named so its row can animate in"
    );
  });

  it("opens straight from showMonitorCreateForm seeded with the current page", async () => {
    const shown = BrowserTestUtils.waitForEvent(
      win.document.getElementById("mainPopupSet"),
      "popupshown"
    );

    AIWindowUI.showMonitorCreateForm(win);

    const panel = (await shown).target;
    Assert.equal(panel.id, PANEL_ID, "showMonitorCreateForm opens the panel");
    const contents = panel.querySelector("agent-monitor-panel");
    await TestUtils.waitForCondition(
      () => contents.view === "create",
      "Panel opens straight to the create view"
    );
    await contents.updateComplete;
    const form = contents.shadowRoot.querySelector("agent-monitor-item");
    await TestUtils.waitForCondition(
      () => form.pageUrls.length === 1,
      "The create form is seeded with the current page"
    );
    Assert.deepEqual(
      form.pageUrls,
      ["https://example.com/"],
      "The seeded page is the one the user is on"
    );
  });

  // The callout's primary button.
  it("opens from the OPEN_SMARTWINDOW_MONITOR_CREATE special message action", async () => {
    const showCreateForm = sandbox.stub(AIWindowUI, "showMonitorCreateForm");

    await SpecialMessageActions.handleAction(
      { type: "OPEN_SMARTWINDOW_MONITOR_CREATE" },
      { documentGlobal: win }
    );

    Assert.ok(
      showCreateForm.calledOnceWith(win),
      "The action opens the create form for the action's window"
    );
  });

  describe("form", () => {
    it("watches the page the user is on as a removable chip", async () => {
      const { contents } = await openMonitorPanel(win);

      const form = await openCreateView(contents);

      // pageUrls is seeded a render before the chip is drawn, so wait for it.
      await TestUtils.waitForCondition(
        () => pageChips(form).length === 1,
        "Waiting for the seeded page to render as a chip"
      );
      Assert.deepEqual(
        form.pageUrls,
        ["https://example.com/"],
        "The form opens watching the page the user is on"
      );
    });

    it("watches nothing when the current page cannot be watched", async () => {
      await promiseNavigateAndLoad(
        win.gBrowser.selectedBrowser,
        "about:robots"
      );
      const { contents } = await openMonitorPanel(win);

      const form = await openCreateView(contents);

      Assert.deepEqual(
        form.pageUrls,
        [],
        "A page no monitor can watch seeds nothing"
      );
    });

    it("labels the seeded page with its title", async () => {
      await promiseNavigateAndLoad(
        win.gBrowser.selectedBrowser,
        TITLED_PAGE_URL
      );
      const pageTitle = win.gBrowser.selectedBrowser.contentTitle;
      Assert.ok(pageTitle, "The open test page has a title");
      const { contents } = await openMonitorPanel(win);

      const form = await openCreateView(contents);

      await TestUtils.waitForCondition(
        () => pageChips(form)[0]?.label === pageTitle,
        "The page the form was opened from is labelled with its title"
      );
    });
  });

  // The header names whichever view is showing and only offers a back button
  // while there is somewhere to go back to.
  describe("header", () => {
    it("has no back button on the list view", async () => {
      const { panel } = await openMonitorPanel(win);

      Assert.ok(
        !panel.querySelector(".subviewbutton-back"),
        "The list view has no back button"
      );
    });

    it("names the create view and offers a labelled back button", async () => {
      const { panel, contents } = await openMonitorPanel(win);

      await openCreateView(contents);

      await win.document.l10n.translateFragment(panel);
      Assert.equal(
        headerTitle(panel).textContent,
        "Create new task",
        "Header names the create view"
      );
      Assert.equal(
        panel.querySelector(".subviewbutton-back")?.getAttribute("aria-label"),
        "Back",
        "Create view offers a back button labelled for assistive technology"
      );
    });

    it("follows the back button to the list view", async () => {
      const { panel, contents } = await openMonitorPanel(win);
      await openCreateView(contents);

      panel.querySelector(".subviewbutton-back").doCommand();

      await assertBackOnListView(panel, contents, "Back");
      Assert.ok(
        !panel.querySelector(".subviewbutton-back"),
        "Back button is removed with the create view so the title stays centered"
      );
    });

    it("follows a cancelled form to the list view", async () => {
      const { panel, contents } = await openMonitorPanel(win);
      await openCreateView(contents);

      contents.shadowRoot.querySelector("agent-monitor-item").dispatchEvent(
        new win.CustomEvent("agent-monitor-item:cancel", {
          detail: {},
          bubbles: true,
          composed: true,
        })
      );

      await assertBackOnListView(panel, contents, "Cancelling");
    });
  });
});

/**
 * Switches an open panel to the create view through its footer row.
 *
 * @param {HTMLElement} contents - The panel's agent-monitor-panel element.
 * @returns {Promise<HTMLElement>} The rendered agent-monitor-item form.
 */
async function openCreateView(contents) {
  contents.shadowRoot.querySelector(".monitor-footer-row").click();
  await contents.updateComplete;
  const form = contents.shadowRoot.querySelector("agent-monitor-item");
  await form?.updateComplete;
  return form;
}

function pageChips(form) {
  return form.shadowRoot.querySelectorAll(".page-pills-row smartwindow-chip");
}

function headerTitle(panel) {
  return panel.querySelector(".panel-header > h1 > span");
}

/**
 * @param {XULPopupElement} panel
 * @param {HTMLElement} contents
 * @param {string} how - The way out of the create view under test.
 */
async function assertBackOnListView(panel, contents, how) {
  await contents.updateComplete;
  await panel.ownerDocument.l10n.translateFragment(panel);
  Assert.equal(contents.view, "list", `${how} returns to the list view`);
  Assert.equal(
    headerTitle(panel).textContent,
    "Tasks",
    `Header names the list view again after ${how}`
  );
}
