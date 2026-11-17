/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

// Tests for split buttton component in urlbar result.

const TEST_RESULTS = [
  new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.TIP,
    source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
    payload: {
      helpUrl: "https://example.com/",
      type: "test",
      titleL10n: { id: "urlbar-search-tips-confirm" },
      buttons: [
        {
          url: "https://example.com/",
          l10n: { id: "urlbar-search-tips-confirm" },
        },
        {
          command: "button-command-1",
          l10n: { id: "urlbar-search-mode-bookmarks" },
          menu: [
            {
              name: "menu-command-1-1",
              l10n: {
                id: "urlbar-result-menu-dismiss-suggestion2",
              },
            },
          ],
        },
        {
          command: "button-command-2",
          l10n: { id: "urlbar-search-mode-tabs" },
          menu: [
            {
              name: "menu-command-2-1",
              l10n: {
                id: "urlbar-result-menu-dismiss-suggestion2",
              },
            },
            {
              name: "menu-command-2-2",
              l10n: {
                id: "urlbar-result-menu-dismiss-suggestion2",
              },
            },
          ],
        },
        {
          url: "https://example.com/",
          l10n: { id: "urlbar-search-tips-confirm-short" },
        },
        {
          command: "button-command-3",
          l10n: { id: "urlbar-search-mode-history" },
          menu: [
            {
              name: "menu-command-3-1",
              l10n: {
                id: "urlbar-result-menu-dismiss-suggestion2",
              },
            },
            {
              name: "menu-command-3-2",
              l10n: {
                id: "urlbar-result-menu-dismiss-suggestion2",
              },
            },
            {
              name: "menu-command-3-3",
              l10n: {
                id: "urlbar-result-menu-dismiss-suggestion2",
              },
            },
          ],
        },
      ],
    },
  }),
];

add_setup(async function () {
  const provider = new UrlbarTestUtils.TestProvider({
    results: TEST_RESULTS,
    priority: 1,
  });
  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  providersManager.registerProvider(provider);
  registerCleanupFunction(() => {
    providersManager.unregisterProvider(provider);
    sinon.restore();
  });
});

add_task(async function layout() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "test",
  });
  Assert.equal(UrlbarTestUtils.getResultCount(window), 1);

  info("Check the ui");
  const { element } = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);

  const firstNormalButton = element.row.querySelector(".urlbarView-button-0");
  Assert.equal(firstNormalButton.dataset.l10nId, "urlbar-search-tips-confirm");
  Assert.ok(!firstNormalButton.classList.contains("urlbarView-splitbutton"));

  const firstSplitButton = firstNormalButton.nextElementSibling;
  Assert.ok(firstSplitButton.classList.contains("urlbarView-splitbutton"));
  const firstSplitButtonMain = firstSplitButton.querySelector(
    ".urlbarView-splitbutton-main"
  );
  Assert.equal(
    firstSplitButtonMain.dataset.l10nId,
    "urlbar-search-mode-bookmarks"
  );
  Assert.equal(firstSplitButtonMain.dataset.command, "button-command-1");
  const firstSplitButtonDropmarker = firstSplitButtonMain.nextElementSibling;
  Assert.ok(
    firstSplitButtonDropmarker.classList.contains(
      "urlbarView-splitbutton-dropmarker"
    )
  );

  const secondSplitButton = firstSplitButton.nextElementSibling;
  Assert.ok(secondSplitButton.classList.contains("urlbarView-splitbutton"));
  const secondSplitButtonMain = secondSplitButton.querySelector(
    ".urlbarView-splitbutton-main"
  );
  Assert.equal(secondSplitButtonMain.dataset.l10nId, "urlbar-search-mode-tabs");
  Assert.equal(secondSplitButtonMain.dataset.command, "button-command-2");
  const secondSplitButtonDropmarker = secondSplitButtonMain.nextElementSibling;
  Assert.ok(
    secondSplitButtonDropmarker.classList.contains(
      "urlbarView-splitbutton-dropmarker"
    )
  );

  const secondNormalButton = secondSplitButton.nextElementSibling;
  Assert.equal(
    secondNormalButton.dataset.l10nId,
    "urlbar-search-tips-confirm-short"
  );
  Assert.ok(!secondNormalButton.classList.contains("urlbarView-splitbutton"));

  const thirdSplitButton = secondNormalButton.nextElementSibling;
  Assert.ok(thirdSplitButton.classList.contains("urlbarView-splitbutton"));
  const thirdSplitButtonMain = thirdSplitButton.querySelector(
    ".urlbarView-splitbutton-main"
  );
  Assert.equal(
    thirdSplitButtonMain.dataset.l10nId,
    "urlbar-search-mode-history"
  );
  Assert.equal(thirdSplitButtonMain.dataset.command, "button-command-3");
  const thirdSplitButtonDropmarker = thirdSplitButtonMain.nextElementSibling;
  Assert.ok(
    thirdSplitButtonDropmarker.classList.contains(
      "urlbarView-splitbutton-dropmarker"
    )
  );

  gURLBar.view.close();
});

add_task(async function activate() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "test",
  });
  const { element } = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);

  const splitButtons = element.row.querySelectorAll(".urlbarView-splitbutton");

  info("Test for first split button");
  await doActivateTest({
    splitButton: splitButtons[0],
    expectedMainCommand: "button-command-1",
    expectedMenuCommands: ["menu-command-1-1"],
  });

  info("Test for second split button");
  await doActivateTest({
    splitButton: splitButtons[1],
    expectedMainCommand: "button-command-2",
    expectedMenuCommands: ["menu-command-2-1", "menu-command-2-2"],
  });

  info("Test for third split button");
  await doActivateTest({
    splitButton: splitButtons[2],
    expectedMainCommand: "button-command-3",
    expectedMenuCommands: [
      "menu-command-3-1",
      "menu-command-3-2",
      "menu-command-3-3",
    ],
  });

  gURLBar.view.close();
});

async function doActivateTest({
  splitButton,
  expectedMainCommand,
  expectedMenuCommands,
}) {
  let engaged;
  sinon.stub(gURLBar.controller.session, "record").callsFake((...args) => {
    engaged = args[1].selType;
  });

  info("Test for main button");
  const splitButtonMain = splitButton.querySelector(
    ".urlbarView-splitbutton-main"
  );
  Assert.equal(splitButtonMain.dataset.command, expectedMainCommand);
  EventUtils.synthesizeMouseAtCenter(splitButtonMain, {});
  await TestUtils.waitForCondition(() => engaged == expectedMainCommand);
  Assert.ok(true, `${expectedMainCommand} is engaged`);

  for (let i = 0; i < expectedMenuCommands.length; i++) {
    info(`Test for menuitem[${i}] in popup`);

    info("Click on dropdown button");
    const popup = gURLBar.view.resultMenu;
    const onPopupShown = BrowserTestUtils.waitForEvent(popup, "shown");
    const dropmarker = splitButton.querySelector(
      ".urlbarView-splitbutton-dropmarker"
    );
    EventUtils.synthesizeMouseAtCenter(dropmarker, {});
    await onPopupShown;

    info("Activate the menuitem");
    const onPopupHidden = BrowserTestUtils.waitForEvent(popup, "hidden");
    const targetMenuItem = popup.querySelector(
      `panel-item:nth-child(${i + 1})`
    );
    EventUtils.synthesizeMouseAtCenter(targetMenuItem, {});
    await onPopupHidden;
    const expectedMenuCommand = expectedMenuCommands[i];
    await TestUtils.waitForCondition(() => engaged == expectedMenuCommand);
    Assert.ok(true, `${expectedMenuCommand} is engaged`);
  }

  sinon.restore();
}
