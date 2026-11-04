/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { PanelTestProvider } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/PanelTestProvider.sys.mjs"
);

const TEST_MESSAGE_ID = "TEST_HNT_CARD_STACK";

let gCardStackMessage;

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      // A preloaded about:newtab would already have asked ASRouter for its
      // message before withTestMessage stubs it, so the card stack would
      // never render in the tab we open.
      ["browser.newtab.preload", false],
      // @nova-cleanup(remove-pref): Nova is enabled by default starting in
      // Firefox 151. Remove this pref override once 151 reaches Release.
      ["browser.newtabpage.activity-stream.nova.enabled", true],
    ],
  });
  NewTabPagePreloading.removePreloadedBrowser(window);

  registerCleanupFunction(async () => {
    await ASRouter.resetMessageState();
  });

  let message = await PanelTestProvider.getMessages().then(msgs =>
    msgs.find(msg => msg.id === TEST_MESSAGE_ID)
  );
  Assert.ok(message, "Found the card stack test message.");
  // The newtab train-hop jobs run this test against an older Firefox, whose
  // copy of the test message can have fewer screens. The stack draws one back
  // card per remaining screen, so always give it two screens after the first.
  const [firstScreen] = message.content.screens;
  gCardStackMessage = {
    ...message,
    content: {
      ...message.content,
      transitions: false,
      screens: [1, 2, 3].map(n => ({
        ...firstScreen,
        id: `${firstScreen.id}_${n}`,
      })),
    },
  };
});

/**
 * Renders the card stack in about:newtab with flat, distinct colors for the
 * front card (blue) and the two back cards (red and green), then samples the
 * painted pixels to check the back cards stay behind the front card.
 *
 * @param {string} dir
 *   The direction to render the card stack in, "ltr" or "rtl".
 */
async function checkCardStackPaintOrder(dir) {
  let sandbox = sinon.createSandbox();

  await withTestMessage(sandbox, gCardStackMessage, async () => {
    await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:newtab");

    await SpecialPowers.spawn(
      gBrowser.selectedBrowser,
      [dir],
      async contentDir => {
        const host = await ContentTaskUtils.waitForCondition(
          () =>
            content.document.querySelector(
              ".asrouter-multistage-message-wrapper"
            ),
          "Waiting for .asrouter-multistage-message-wrapper"
        );
        const shadow = Cu.waiveXrays(host).shadowRoot;
        const screen = await ContentTaskUtils.waitForCondition(
          () => shadow?.querySelector(".screen[pos='card-stack']"),
          "Waiting for the card stack screen to render"
        );

        host.setAttribute("dir", contentDir);
        const style = content.document.createElement("style");
        style.textContent = `
        *, *::before, *::after {
          animation: none !important;
          transition: none !important;
        }
        .screen[pos='card-stack'] {
          --background-color-box: rgb(0, 0, 255) !important;
          --card-stack-color-1: rgb(0, 0, 255) !important;
          --card-stack-flare: rgb(0, 0, 255) !important;
          --card-stack-color-2: rgb(255, 0, 0) !important;
          --card-stack-color-3: rgb(0, 255, 0) !important;
          --card-stack-border: 0 none !important;
        }
        .screen[pos='card-stack'] > :not(.card-stack-front, .card-stack-layer-1, .card-stack-layer-2) {
          visibility: hidden !important;
        }
      `;
        shadow.appendChild(style);

        screen.scrollIntoView({ block: "center" });
        Assert.equal(
          content.getComputedStyle(screen).direction,
          contentDir,
          `The card stack renders as ${contentDir}`
        );

        const rect = screen.getBoundingClientRect();
        const x = rect.left + rect.width / 2;
        const canvas = content.document.createElementNS(
          "http://www.w3.org/1999/xhtml",
          "canvas"
        );
        canvas.width = 1;
        canvas.height = 1;
        const ctx = canvas.getContext("2d");
        const colorAt = y => {
          ctx.drawWindow(
            content,
            x + content.scrollX,
            y + content.scrollY,
            1,
            1,
            "rgb(255, 255, 255)"
          );
          const [r, g, b] = ctx.getImageData(0, 0, 1, 1).data;
          return { r, g, b };
        };
        const isColor = ({ r, g, b }, channel) => {
          const values = { r, g, b };
          return Object.entries(values).every(([key, value]) =>
            key === channel ? value > 200 : value < 55
          );
        };

        const front = colorAt(rect.top + rect.height / 2);
        Assert.ok(
          isColor(front, "b"),
          `The front card is painted above the back cards (${contentDir}), got ${JSON.stringify(front)}`
        );
        const firstBack = colorAt(rect.bottom + 4);
        Assert.ok(
          isColor(firstBack, "r"),
          `The first back card peeks out below the front card (${contentDir}), got ${JSON.stringify(firstBack)}`
        );
        const secondBack = colorAt(rect.bottom + 12);
        Assert.ok(
          isColor(secondBack, "g"),
          `The second back card peeks out below the first (${contentDir}), got ${JSON.stringify(secondBack)}`
        );
      }
    );
  });

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  sandbox.restore();
}

add_task(async function test_card_stack_paint_order_ltr() {
  await checkCardStackPaintOrder("ltr");
});

add_task(async function test_card_stack_paint_order_rtl() {
  await checkCardStackPaintOrder("rtl");
});

add_task(async function test_card_stack_needsAwait_waits_for_qr_dialog() {
  let sandbox = sinon.createSandbox();
  const [firstScreen, ...otherScreens] = gCardStackMessage.content.screens;
  const message = {
    ...gCardStackMessage,
    content: {
      ...gCardStackMessage.content,
      screens: [
        {
          ...firstScreen,
          content: {
            ...firstScreen.content,
            primary_button: {
              ...firstScreen.content.primary_button,
              action: {
                type: "SHOW_QR_CODE",
                data: { args: "https://example.com/" },
                needsAwait: true,
                navigate: true,
              },
            },
          },
        },
        ...otherScreens,
      ],
    },
  };

  await withTestMessage(sandbox, message, async () => {
    let tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      "about:newtab"
    );
    let manager = gBrowser
      .getTabDialogBox(tab.linkedBrowser)
      .getTabDialogManager();

    const screenIsShowing = screenId =>
      SpecialPowers.spawn(tab.linkedBrowser, [screenId], async id => {
        const host = await ContentTaskUtils.waitForCondition(() =>
          content.document.querySelector(".asrouter-multistage-message-wrapper")
        );
        const shadow = Cu.waiveXrays(host).shadowRoot;
        await ContentTaskUtils.waitForCondition(
          () => shadow?.querySelector(`.screen.${id}`),
          `Waiting for ${id}`
        );
      });

    await screenIsShowing(firstScreen.id);
    await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
      const host = content.document.querySelector(
        ".asrouter-multistage-message-wrapper"
      );
      Cu.waiveXrays(host).shadowRoot.querySelector("button.primary").click();
    });

    await TestUtils.waitForCondition(
      () => manager._dialogs.length,
      "Waiting for the QR code dialog to open"
    );
    let [dialog] = manager._dialogs;
    await dialog._dialogReady;

    let stillOnFirstScreen = await SpecialPowers.spawn(
      tab.linkedBrowser,
      [firstScreen.id],
      id =>
        !!Cu.waiveXrays(
          content.document.querySelector(".asrouter-multistage-message-wrapper")
        ).shadowRoot.querySelector(`.screen.${id}`)
    );
    Assert.ok(
      stillOnFirstScreen,
      "The card stack stays on the first card while the dialog is open"
    );

    dialog.close();
    await screenIsShowing(otherScreens[0].id);
    Assert.ok(true, "The card stack advanced after the dialog closed");

    BrowserTestUtils.removeTab(tab);
  });
  sandbox.restore();
});
