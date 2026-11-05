/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AboutWelcomeTelemetry } = ChromeUtils.importESModule(
  "resource:///modules/aboutwelcome/AboutWelcomeTelemetry.sys.mjs"
);

const { PanelTestProvider } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/PanelTestProvider.sys.mjs"
);

const TEST_MESSAGE_ID = "TEST_ASROUTER_MULTISTAGE_MESSAGE";

let gTestMultistageMessage;

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.newtab.preload", false],
      ["browser.newtabpage.activity-stream.telemetry", true],
      // @nova-cleanup(remove-pref): Nova is enabled by default starting in
      // Firefox 151. Remove this pref override once 151 reaches Release.
      ["browser.newtabpage.activity-stream.nova.enabled", true],
    ],
  });
  NewTabPagePreloading.removePreloadedBrowser(window);

  Services.fog.testResetFOG();
  registerCleanupFunction(async () => {
    Services.fog.testResetFOG();
    await ASRouter.resetMessageState();
  });

  gTestMultistageMessage = await PanelTestProvider.getMessages().then(msgs =>
    msgs.find(msg => msg.id === TEST_MESSAGE_ID)
  );
  Assert.ok(gTestMultistageMessage, "Found a test multistage message to use.");
});

/**
 * Tests that registering our test message results in the multistage message
 * component embedding in about:newtab.
 */
add_task(async function test_show_multistage_message() {
  let sandbox = sinon.createSandbox();

  await withTestMessage(sandbox, gTestMultistageMessage, async () => {
    await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:newtab");

    await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async () => {
      const wrapper = await ContentTaskUtils.waitForCondition(() => {
        return content.document.querySelector(
          ".asrouter-multistage-message-wrapper"
        );
      }, "Waiting for .asrouter-multistage-message-wrapper");

      await ContentTaskUtils.waitForCondition(() => {
        return wrapper.shadowRoot?.querySelector(".multistage-newtab-wrapper");
      }, "Waiting for React bundle to mount in shadow root");
    });
  });

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  sandbox.restore();
});

/**
 * Tests that clicking the dismiss button in the component's shadow DOM
 * permanently blocks the message.
 */
add_task(async function test_dismiss_button_blocks_message() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(ASRouter, "blockMessageById").returns(Promise.resolve());

  await withTestMessage(sandbox, gTestMultistageMessage, async () => {
    await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:newtab");

    await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async () => {
      const wrapper = await ContentTaskUtils.waitForCondition(() => {
        return content.document.querySelector(
          ".asrouter-multistage-message-wrapper"
        );
      }, "Waiting for .asrouter-multistage-message-wrapper");

      await ContentTaskUtils.waitForCondition(() => {
        return Cu.waiveXrays(wrapper).shadowRoot?.querySelector(
          ".multistage-newtab-wrapper"
        );
      }, "Waiting for React bundle to mount in shadow root");

      let shadow = Cu.waiveXrays(wrapper).shadowRoot;
      let dismissBtn = shadow.querySelector(
        ".multistage-newtab-wrapper > moz-button"
      );
      Assert.ok(dismissBtn, "Found dismiss button in shadow root");
      dismissBtn.click();

      await ContentTaskUtils.waitForCondition(() => {
        return !content.document.querySelector(
          ".asrouter-multistage-message-wrapper"
        );
      }, ".asrouter-multistage-message-wrapper removed from DOM");
    });
  });

  Assert.ok(
    ASRouter.blockMessageById.calledWith(TEST_MESSAGE_ID),
    "The test message was blocked."
  );
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  sandbox.restore();
});

/**
 * Tests that showing the multistage message records an IMPRESSION telemetry
 * event and adds an ASRouter impression for frequency capping.
 */
add_task(async function test_impression_telemetry() {
  let sandbox = sinon.createSandbox();
  sandbox.spy(ASRouter, "addImpression");
  sandbox.spy(AboutWelcomeTelemetry.prototype, "submitGleanPingForPing");

  await withTestMessage(sandbox, gTestMultistageMessage, async () => {
    await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:newtab");

    await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async () => {
      const wrapper = await ContentTaskUtils.waitForCondition(() => {
        return content.document.querySelector(
          ".asrouter-multistage-message-wrapper"
        );
      }, "Waiting for .asrouter-multistage-message-wrapper");

      await ContentTaskUtils.waitForCondition(() => {
        return wrapper.shadowRoot?.querySelector(".multistage-newtab-wrapper");
      }, "Waiting for React bundle to mount in shadow root");

      // newtabmessage only triggers a impression once 30% of the element is in view
      wrapper.scrollIntoView({ block: "center" });
    });
  });

  await TestUtils.waitForCondition(
    () => ASRouter.addImpression.calledWith(gTestMultistageMessage),
    "The test message had an impression recorded for it."
  );
  await TestUtils.waitForCondition(
    () =>
      AboutWelcomeTelemetry.prototype.submitGleanPingForPing.calledWithMatch(
        sinon.match({
          message_id: TEST_MESSAGE_ID,
          event: "IMPRESSION",
          pingType: "newtab_message",
        })
      ),
    "Showing the message recorded an IMPRESSION telemetry event."
  );
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  sandbox.restore();
});

/**
 * Tests that showing the first screen of the multistage message adds an
 * ASRouter screen impression for per-screen frequency capping.
 */
add_task(async function test_screen_impression() {
  let sandbox = sinon.createSandbox();
  sandbox.spy(ASRouter, "addScreenImpression");

  await withTestMessage(sandbox, gTestMultistageMessage, async () => {
    await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:newtab");

    await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async () => {
      const wrapper = await ContentTaskUtils.waitForCondition(() => {
        return content.document.querySelector(
          ".asrouter-multistage-message-wrapper"
        );
      }, "Waiting for .asrouter-multistage-message-wrapper");

      await ContentTaskUtils.waitForCondition(() => {
        return wrapper.shadowRoot?.querySelector(".multistage-newtab-wrapper");
      }, "Waiting for React bundle to mount in shadow root");
    });
  });

  await TestUtils.waitForCondition(
    () =>
      ASRouter.addScreenImpression.calledWithMatch(
        sinon.match({ id: "SCREEN_1" })
      ),
    "The first screen had a screen impression recorded for it."
  );
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  sandbox.restore();
});

/**
 * Tests that showing the first screen of the multistage message records a
 * per-screen IMPRESSION telemetry event.
 */
add_task(async function test_screen_impression_telemetry() {
  let sandbox = sinon.createSandbox();
  sandbox.spy(AboutWelcomeTelemetry.prototype, "submitGleanPingForPing");

  await withTestMessage(sandbox, gTestMultistageMessage, async () => {
    await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:newtab");

    await TestUtils.waitForCondition(
      () =>
        AboutWelcomeTelemetry.prototype.submitGleanPingForPing.calledWithMatch(
          sinon.match({
            message_id: sinon.match(`${TEST_MESSAGE_ID}_0_SCREEN_1`),
            event: "IMPRESSION",
            pingType: "newtab_message",
          })
        ),
      "The first screen recorded an IMPRESSION telemetry event."
    );
  });

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  sandbox.restore();
});

/**
 * Tests that no CLICK telemetry is recorded when the multistage message first
 * renders. Regression test for the AW handler bridge incorrectly routing
 * impression signals through handleClick.
 */
add_task(async function test_no_click_telemetry_on_render() {
  let sandbox = sinon.createSandbox();
  sandbox.spy(AboutWelcomeTelemetry.prototype, "submitGleanPingForPing");

  await withTestMessage(sandbox, gTestMultistageMessage, async () => {
    await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:newtab");

    await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async () => {
      const wrapper = await ContentTaskUtils.waitForCondition(() => {
        return content.document.querySelector(
          ".asrouter-multistage-message-wrapper"
        );
      }, "Waiting for .asrouter-multistage-message-wrapper");

      await ContentTaskUtils.waitForCondition(() => {
        return wrapper.shadowRoot?.querySelector(".multistage-newtab-wrapper");
      }, "Waiting for React bundle to mount in shadow root");

      // newtabmessage only triggers a impression once 30% of the element is in view
      wrapper.scrollIntoView({ block: "center" });
    });
  });

  // Wait for the IMPRESSION ping first to confirm all initial AW* handlers
  // have fired, then verify no CLICK was recorded alongside it.
  await TestUtils.waitForCondition(
    () =>
      AboutWelcomeTelemetry.prototype.submitGleanPingForPing.calledWithMatch(
        sinon.match({ event: "IMPRESSION" })
      ),
    "Waiting for IMPRESSION before checking for spurious CLICKs"
  );
  Assert.ok(
    !AboutWelcomeTelemetry.prototype.submitGleanPingForPing.calledWithMatch(
      sinon.match({ event: sinon.match(/^CLICK/) })
    ),
    "No CLICK telemetry was fired on initial render."
  );
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  sandbox.restore();
});

/**
 * Tests that clicking the primary and secondary buttons within the multistage
 * message records a CLICK_BUTTON telemetry event with the screen-specific
 * message id and the button as the source.
 */
add_task(async function test_primary_button_click_telemetry() {
  let sandbox = sinon.createSandbox();
  sandbox.spy(AboutWelcomeTelemetry.prototype, "submitGleanPingForPing");

  // Use a simple message with an inert primary button action so clicking it
  // fires telemetry without navigating or opening new tabs.
  let inertMessage = {
    ...gTestMultistageMessage,
    content: {
      messageType: "ASRouterMultistageMessage",
      id: TEST_MESSAGE_ID,
      transitions: false,
      backdrop: "transparent",
      screens: [
        {
          id: "SCREEN_1",
          content: {
            position: "center",
            title: { raw: "Test Title" },
            primary_button: {
              label: { raw: "Primary" },
              action: {},
            },
            secondary_button: {
              label: { raw: "Secondary" },
              action: {},
            },
          },
        },
      ],
    },
  };

  await withTestMessage(sandbox, inertMessage, async () => {
    await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:newtab");

    await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async () => {
      const wrapper = await ContentTaskUtils.waitForCondition(() => {
        return content.document.querySelector(
          ".asrouter-multistage-message-wrapper"
        );
      }, "Waiting for .asrouter-multistage-message-wrapper");

      await ContentTaskUtils.waitForCondition(() => {
        return Cu.waiveXrays(wrapper).shadowRoot?.querySelector(
          ".multistage-newtab-wrapper"
        );
      }, "Waiting for React bundle to mount in shadow root");

      let shadow = Cu.waiveXrays(wrapper).shadowRoot;
      let primaryBtn = shadow.querySelector("button.primary");
      Assert.ok(primaryBtn, "Found primary button in shadow root");
      primaryBtn.click();
      let secondaryBtn = await ContentTaskUtils.waitForCondition(
        () => shadow.querySelector("button.secondary"),
        "Waiting for secondary button in shadow root"
      );
      secondaryBtn.click();
    });
  });

  for (const source of ["primary_button", "secondary_button"]) {
    await TestUtils.waitForCondition(
      () =>
        AboutWelcomeTelemetry.prototype.submitGleanPingForPing.calledWithMatch(
          sinon.match({
            message_id: `${TEST_MESSAGE_ID}_0_SCREEN_1`,
            event: "CLICK_BUTTON",
            event_context: sinon.match({ source }),
            pingType: "newtab_message",
          })
        ),
      `Clicking ${source} recorded a CLICK_BUTTON telemetry event.`
    );
  }
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  sandbox.restore();
});

/**
 * Tests that clicking the dismiss button records a DISMISS telemetry event.
 */
add_task(async function test_dismiss_button_telemetry() {
  let sandbox = sinon.createSandbox();
  sandbox.spy(AboutWelcomeTelemetry.prototype, "submitGleanPingForPing");
  sandbox.stub(ASRouter, "blockMessageById").returns(Promise.resolve());

  await withTestMessage(sandbox, gTestMultistageMessage, async () => {
    await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:newtab");

    await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async () => {
      const wrapper = await ContentTaskUtils.waitForCondition(() => {
        return content.document.querySelector(
          ".asrouter-multistage-message-wrapper"
        );
      }, "Waiting for .asrouter-multistage-message-wrapper");

      await ContentTaskUtils.waitForCondition(() => {
        return Cu.waiveXrays(wrapper).shadowRoot?.querySelector(
          ".multistage-newtab-wrapper"
        );
      }, "Waiting for React bundle to mount in shadow root");

      let shadow = Cu.waiveXrays(wrapper).shadowRoot;
      let dismissBtn = shadow.querySelector(
        ".multistage-newtab-wrapper > moz-button"
      );
      Assert.ok(dismissBtn, "Found dismiss button in shadow root");
      dismissBtn.click();

      await ContentTaskUtils.waitForCondition(() => {
        return !content.document.querySelector(
          ".asrouter-multistage-message-wrapper"
        );
      }, ".asrouter-multistage-message-wrapper removed from DOM");
    });
  });

  Assert.ok(
    AboutWelcomeTelemetry.prototype.submitGleanPingForPing.calledWithMatch(
      sinon.match({
        message_id: TEST_MESSAGE_ID,
        event: "DISMISS",
        pingType: "newtab_message",
      })
    ),
    "Clicking the dismiss button recorded a DISMISS telemetry event."
  );
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  sandbox.restore();
});

/**
 * Tests that a card stack auto-dismissing on its last card records a single
 * DISMISS telemetry event carrying the resolution reason and the completion
 * status of each shown card.
 */
add_task(async function test_card_stack_auto_dismiss_telemetry() {
  let sandbox = sinon.createSandbox();
  sandbox.spy(AboutWelcomeTelemetry.prototype, "submitGleanPingForPing");

  const makeCard = (id, targeting) => ({
    id,
    targeting,
    content: {
      position: "card-stack",
      title: { raw: id },
      primary_button: {
        label: { raw: "Primary" },
        action: { navigate: true },
      },
      secondary_button: {
        label: { raw: "Secondary" },
        action: { navigate: true },
      },
    },
  });

  let cardStackMessage = {
    ...gTestMultistageMessage,
    content: {
      messageType: "ASRouterMultistageMessage",
      id: TEST_MESSAGE_ID,
      transitions: true,
      backdrop: "transparent",
      screens: [
        makeCard("CARD_1"),
        makeCard("CARD_HIDDEN", "false"),
        makeCard("CARD_2"),
        {
          id: "CARD_LAST",
          auto_dismiss: { after: 0 },
          content: {
            position: "card-stack",
            title: { raw: "CARD_LAST" },
          },
        },
      ],
    },
  };

  await withTestMessage(sandbox, cardStackMessage, async () => {
    await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:newtab");

    await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async () => {
      const wrapper = await ContentTaskUtils.waitForCondition(() => {
        return content.document.querySelector(
          ".asrouter-multistage-message-wrapper"
        );
      }, "Waiting for .asrouter-multistage-message-wrapper");

      const shadow = await ContentTaskUtils.waitForCondition(() => {
        let root = Cu.waiveXrays(wrapper).shadowRoot;
        return root?.querySelector(".multistage-newtab-wrapper") && root;
      }, "Waiting for React bundle to mount in shadow root");

      const waitForCard = (id, selector = "") =>
        ContentTaskUtils.waitForCondition(
          () => shadow.querySelector(`.screen.${id} ${selector}`.trim()),
          `Waiting for ${id} ${selector}`
        );

      (await waitForCard("CARD_1", "button.primary")).click();
      (await waitForCard("CARD_2", "button.secondary")).click();
      await waitForCard("CARD_LAST");

      await ContentTaskUtils.waitForCondition(() => {
        return !content.document.querySelector(
          ".asrouter-multistage-message-wrapper"
        );
      }, "Card stack auto-dismissed and was removed from DOM");
    });
  });

  const spy = AboutWelcomeTelemetry.prototype.submitGleanPingForPing;
  const dismissPings = () =>
    spy
      .getCalls()
      .map(call => call.args[0])
      .filter(ping => ping.event === "DISMISS");
  await TestUtils.waitForCondition(
    () => dismissPings().length,
    "Waiting for the DISMISS telemetry event"
  );

  Assert.ok(
    spy.calledWithMatch(
      sinon.match({
        event: "AUTO_DISMISS",
        message_id: `${TEST_MESSAGE_ID}_2_CARD_LAST`,
      })
    ),
    "The last card recorded an AUTO_DISMISS event."
  );
  const pings = dismissPings();
  Assert.equal(
    pings.length,
    1,
    "Only one DISMISS telemetry event was recorded."
  );
  const [ping] = pings;
  Assert.equal(
    ping.message_id,
    TEST_MESSAGE_ID,
    "DISMISS uses the message id."
  );
  Assert.equal(ping.pingType, "newtab_message", "DISMISS is a newtab_message.");
  Assert.equal(ping.event_context.reason, "engaged", "Resolution reason.");
  Assert.equal(ping.event_context.completion_count, 1, "Completion count.");
  Assert.deepEqual(
    ping.event_context.completion_status,
    { CARD_1: "completed", CARD_2: "skipped" },
    "Completion status covers each shown card, not hidden or button-less ones."
  );
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  sandbox.restore();
});
