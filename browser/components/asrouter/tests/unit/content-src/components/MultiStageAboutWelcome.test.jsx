import { GlobalOverrider } from "tests/unit/utils";
import {
  MultiStageAboutWelcome,
  SecondaryCTA,
  StepsIndicator,
  ProgressBar,
  WelcomeScreen,
} from "content-src/components/MultiStageAboutWelcome";
import { SingleSelect } from "content-src/components/SingleSelect";
import React from "react";
import { shallow, mount } from "enzyme";
import { AboutWelcomeDefaults } from "modules/AboutWelcomeDefaults.sys.mjs";
import { MultiStageUtils } from "content-src/lib/multistage-utils.mjs";

const spinEventLoop = async () => {
  // Spin the event loop to allow the useEffect hooks to execute,
  // any promises to resolve, and re-rendering to happen after the
  // promises have updated the state/props
  await new Promise(resolve => setTimeout(resolve, 0));
};

describe("MultiStageAboutWelcome module", () => {
  let globals;
  let sandbox;

  const DEFAULT_PROPS = {
    defaultScreens: AboutWelcomeDefaults.getDefaults().screens,
    metricsFlowUri: "http://localhost/",
    message_id: "DEFAULT_ABOUTWELCOME",
    utm_term: "default",
    startScreen: 0,
  };

  beforeEach(async () => {
    globals = new GlobalOverrider();
    globals.set({
      AWEvaluateScreenTargeting: () => {},
      AWGetSelectedTheme: () => Promise.resolve("automatic"),
      AWGetActiveThemeId: () => Promise.resolve("nova-flare@mozilla.org"),
      AWGetInstalledAddons: () => Promise.resolve(["test-addon-id"]),
      AWGetUnhandledCampaignAction: () => Promise.resolve(false),
      AWSendEventTelemetry: () => {},
      AWSendToParent: () => {},
      AWWaitForMigrationClose: () => Promise.resolve(),
      AWSelectTheme: () => Promise.resolve(),
      AWFinish: () => Promise.resolve(),
      AWEvaluateAttributeTargeting: () => Promise.resolve(false),
    });
    sandbox = sinon.createSandbox();
  });

  afterEach(() => {
    sandbox.restore();
    globals.restore();
  });

  describe("MultiStageAboutWelcome functional component", () => {
    it("should render MultiStageAboutWelcome", () => {
      const wrapper = shallow(<MultiStageAboutWelcome {...DEFAULT_PROPS} />);

      assert.ok(wrapper.exists());
    });

    it("should pass activeTheme and initialTheme props to WelcomeScreen", async () => {
      let wrapper = mount(<MultiStageAboutWelcome {...DEFAULT_PROPS} />);
      await spinEventLoop();
      // sync up enzyme's representation with the real DOM
      wrapper.update();

      let welcomeScreenWrapper = wrapper.find(WelcomeScreen);
      assert.strictEqual(welcomeScreenWrapper.prop("activeTheme"), "automatic");
      assert.strictEqual(
        welcomeScreenWrapper.prop("initialTheme"),
        "automatic"
      );
    });

    it("should pass activeThemeId prop to WelcomeScreen", async () => {
      let wrapper = mount(<MultiStageAboutWelcome {...DEFAULT_PROPS} />);
      await spinEventLoop();
      wrapper.update();

      let welcomeScreenWrapper = wrapper.find(WelcomeScreen);
      assert.strictEqual(
        welcomeScreenWrapper.prop("activeThemeId"),
        "nova-flare@mozilla.org"
      );
    });

    it("should fetch a list of installed Addons", async () => {
      let wrapper = mount(<MultiStageAboutWelcome {...DEFAULT_PROPS} />);
      await spinEventLoop();
      // sync up enzyme's representation with the real DOM
      wrapper.update();

      let welcomeScreenWrapper = wrapper.find(WelcomeScreen);
      assert.strictEqual(
        welcomeScreenWrapper.prop("installedAddons")[0],
        "test-addon-id"
      );
    });

    it("should handle primary Action", () => {
      const screens = [
        {
          content: {
            title: "test title",
            subtitle: "test subtitle",
            primary_button: {
              label: "Test button",
              action: {
                navigate: true,
              },
            },
          },
        },
      ];

      const PRIMARY_ACTION_PROPS = {
        defaultScreens: screens,
        metricsFlowUri: "http://localhost/",
        message_id: "DEFAULT_ABOUTWELCOME",
        utm_term: "default",
        startScreen: 0,
      };

      const stub = sinon.stub(MultiStageUtils, "sendActionTelemetry");
      let wrapper = mount(<MultiStageAboutWelcome {...PRIMARY_ACTION_PROPS} />);
      wrapper.update();

      let welcomeScreenWrapper = wrapper.find(WelcomeScreen);
      const btnPrimary = welcomeScreenWrapper.find(".primary");
      btnPrimary.simulate("click");
      assert.calledOnce(stub);
      assert.equal(
        stub.firstCall.args[0],
        welcomeScreenWrapper.props().messageId
      );
      assert.equal(stub.firstCall.args[1], "primary_button");
      stub.restore();
    });

    it("should dismiss a screen with auto_dismiss after the configured time", () => {
      let clock = sinon.useFakeTimers();
      const screens = [
        {
          id: "LAST_CARD",
          auto_dismiss: { after: 6000 },
          content: {
            position: "card-stack",
            layout: "last-card",
            title: "test title",
            subtitle: "test subtitle",
          },
        },
      ];
      const finishStub = sandbox.stub(global, "AWFinish");
      const telemetryStub = sandbox.stub(
        MultiStageUtils,
        "sendActionTelemetry"
      );
      const wrapper = mount(
        <MultiStageAboutWelcome
          defaultScreens={screens}
          message_id="TEST_MESSAGE"
          startScreen={0}
        />
      );

      clock.tick(5999);
      assert.notCalled(finishStub);
      clock.tick(1);
      assert.calledOnce(finishStub);
      assert.calledOnceWithExactly(
        telemetryStub,
        "TEST_MESSAGE_0_LAST_CARD",
        "auto_dismiss",
        "AUTO_DISMISS"
      );

      wrapper.unmount();
      clock.restore();
    });

    it("should autoAdvance on last screen and send appropriate telemetry", () => {
      let clock = sinon.useFakeTimers();
      const screens = [
        {
          auto_advance: "primary_button",
          content: {
            title: "test title",
            subtitle: "test subtitle",
            primary_button: {
              label: "Test Button",
              action: {
                navigate: true,
              },
            },
          },
        },
      ];
      const AUTO_ADVANCE_PROPS = {
        defaultScreens: screens,
        metricsFlowUri: "http://localhost/",
        message_id: "DEFAULT_ABOUTWELCOME",
        utm_term: "default",
        startScreen: 0,
      };
      const wrapper = mount(<MultiStageAboutWelcome {...AUTO_ADVANCE_PROPS} />);
      wrapper.update();
      const finishStub = sandbox.stub(global, "AWFinish");
      const telemetryStub = sinon.stub(MultiStageUtils, "sendActionTelemetry");

      assert.notCalled(finishStub);
      clock.tick(20001);
      assert.calledOnce(finishStub);
      assert.calledOnce(telemetryStub);
      assert.equal(telemetryStub.lastCall.args[2], "AUTO_ADVANCE");
      clock.restore();
      finishStub.restore();
      telemetryStub.restore();
    });

    describe("out transition timing", () => {
      let clock;

      // Restored here rather than at the end of each test so that an assertion
      // failure can't leak fake timers into the rest of the suite.
      afterEach(() => {
        clock?.restore();
        clock = null;
      });

      const transitionScreens = position => [
        {
          id: "SCREEN_1",
          content: {
            position,
            title: "screen 1",
            primary_button: {
              label: "Next",
              action: { navigate: true },
            },
          },
        },
        {
          id: "SCREEN_2",
          content: { position, title: "screen 2" },
        },
      ];

      // Mounts a two screen message, clicks through the first screen, and
      // hands back a clock stopped at the moment the out transition began.
      const clickThroughFirstScreen = async position => {
        const wrapper = mount(
          <MultiStageAboutWelcome
            {...DEFAULT_PROPS}
            defaultScreens={transitionScreens(position)}
            transitions={true}
          />
        );
        await spinEventLoop();
        wrapper.update();

        clock = sinon.useFakeTimers();
        wrapper.find("button.primary").simulate("click");
        wrapper.update();
        return wrapper;
      };

      const currentScreen = wrapper =>
        wrapper.find("main.screen").prop("className");

      it("should advance a card-stack screen after 400ms", async () => {
        const wrapper = await clickThroughFirstScreen("card-stack");

        clock.tick(399);
        wrapper.update();
        assert.include(currentScreen(wrapper), "SCREEN_1");

        clock.tick(2);
        wrapper.update();
        assert.include(currentScreen(wrapper), "SCREEN_2");
      });

      it("should only advance once when navigating before the in transition ends", () => {
        clock = sinon.useFakeTimers();
        const wrapper = mount(
          <MultiStageAboutWelcome
            {...DEFAULT_PROPS}
            defaultScreens={[
              ...transitionScreens("split"),
              { id: "SCREEN_3", content: { position: "split", title: "3" } },
            ]}
            transitions={true}
          />
        );

        wrapper.find("button.primary").simulate("click");
        clock.tick(100);
        wrapper.update();
        wrapper.find("button.primary").simulate("click");

        clock.tick(2000);
        wrapper.update();
        assert.include(currentScreen(wrapper), "SCREEN_2");
      });

      it("should advance other positions after the default 1000ms", async () => {
        const wrapper = await clickThroughFirstScreen("split");

        clock.tick(999);
        wrapper.update();
        assert.include(currentScreen(wrapper), "SCREEN_1");

        clock.tick(2);
        wrapper.update();
        assert.include(currentScreen(wrapper), "SCREEN_2");
      });

      // The card stack's exit animation is owned by MultistageWithDismiss, so
      // waiting here too would leave an empty slot for the duration.
      const mountLastScreenOnly = async position => {
        const wrapper = mount(
          <MultiStageAboutWelcome
            {...DEFAULT_PROPS}
            defaultScreens={[
              {
                id: "ONLY_SCREEN",
                content: {
                  position,
                  title: "only screen",
                  primary_button: {
                    label: "Done",
                    action: { navigate: true },
                  },
                },
              },
            ]}
            transitions={true}
          />
        );
        await spinEventLoop();
        wrapper.update();
        return wrapper;
      };

      it("should finish immediately from the last card-stack screen", async () => {
        const wrapper = await mountLastScreenOnly("card-stack");
        const finishStub = sandbox.stub(global, "AWFinish");
        clock = sinon.useFakeTimers();

        wrapper.find("button.primary").simulate("click");

        assert.calledOnce(finishStub);
      });

      it("should still defer finishing for other positions", async () => {
        const wrapper = await mountLastScreenOnly("split");
        const finishStub = sandbox.stub(global, "AWFinish");
        clock = sinon.useFakeTimers();

        wrapper.find("button.primary").simulate("click");
        assert.notCalled(finishStub);

        clock.tick(1001);
        assert.calledOnce(finishStub);
      });
    });

    it("should autoAdvance with configurable time and send appropriate telemetry", () => {
      let clock = sinon.useFakeTimers();
      const screens = [
        {
          auto_advance: {
            actionEl: "primary_button",
            actionTimeMS: 5000,
          },
          content: {
            title: "test title",
            subtitle: "test subtitle",
            primary_button: {
              label: "Test Button",
              action: {
                navigate: true,
              },
            },
          },
        },
      ];
      const AUTO_ADVANCE_PROPS = {
        defaultScreens: screens,
        metricsFlowUri: "http://localhost/",
        message_id: "DEFAULT_ABOUTWELCOME",
        utm_term: "default",
        startScreen: 0,
      };
      const wrapper = mount(<MultiStageAboutWelcome {...AUTO_ADVANCE_PROPS} />);
      wrapper.update();
      const finishStub = sandbox.stub(global, "AWFinish");
      const telemetryStub = sinon.stub(MultiStageUtils, "sendActionTelemetry");

      assert.notCalled(finishStub);
      clock.tick(5001);
      assert.calledOnce(finishStub);
      assert.calledOnce(telemetryStub);
      assert.equal(telemetryStub.lastCall.args[2], "AUTO_ADVANCE");
      clock.restore();
      finishStub.restore();
      telemetryStub.restore();
    });

    it("should send telemetry ping on collectSelect", () => {
      const screens = [
        {
          id: "EASY_SETUP_TEST",
          content: {
            tiles: {
              type: "multiselect",
              data: [
                {
                  id: "checkbox-1",
                  defaultValue: true,
                },
              ],
            },
            primary_button: {
              label: "Test Button",
              action: {
                collectSelect: true,
              },
            },
          },
        },
      ];
      const EASY_SETUP_PROPS = {
        defaultScreens: screens,
        message_id: "DEFAULT_ABOUTWELCOME",
        startScreen: 0,
      };
      const stub = sinon.stub(MultiStageUtils, "sendActionTelemetry");
      let wrapper = mount(<MultiStageAboutWelcome {...EASY_SETUP_PROPS} />);
      wrapper.update();

      let welcomeScreenWrapper = wrapper.find(WelcomeScreen);
      const btnPrimary = welcomeScreenWrapper.find(".primary");
      btnPrimary.simulate("click");
      assert.calledTwice(stub);
      assert.equal(
        stub.firstCall.args[0],
        welcomeScreenWrapper.props().messageId
      );
      assert.equal(stub.firstCall.args[1], "primary_button");
      assert.equal(
        stub.lastCall.args[0],
        welcomeScreenWrapper.props().messageId
      );
      assert.ok(stub.lastCall.args[1].includes("checkbox-1"));
      assert.equal(stub.lastCall.args[2], "SELECT_CHECKBOX");
      stub.restore();
    });

    it("should send telemetry ping on collectTextInput", () => {
      const screens = [
        {
          id: "TEXT_INPUT_TEST",
          content: {
            tiles: {
              type: "textarea",
              data: {
                id: "text-input-test",
              },
            },
            primary_button: {
              label: "Test Button",
              action: {
                collectTextInput: true,
              },
            },
          },
        },
      ];
      const COMPONENT_PROPS = {
        defaultScreens: screens,
        message_id: "DEFAULT_ABOUTWELCOME",
        startScreen: 0,
      };
      const stub = sinon.stub(MultiStageUtils, "sendActionTelemetry");
      let wrapper = mount(<MultiStageAboutWelcome {...COMPONENT_PROPS} />);
      wrapper.update();

      let welcomeScreenWrapper = wrapper.find(WelcomeScreen);
      // Set some text in the text area to simulate user input
      const textArea = welcomeScreenWrapper.find("textarea.textarea-input");
      textArea.simulate("change", {
        target: { value: "Some user input text" },
      });
      wrapper.update();
      welcomeScreenWrapper = wrapper.find(WelcomeScreen);

      const btnPrimary = welcomeScreenWrapper.find(".primary");
      btnPrimary.simulate("click");
      assert.calledTwice(stub);
      assert.equal(
        stub.firstCall.args[0],
        welcomeScreenWrapper.props().messageId
      );
      assert.equal(stub.firstCall.args[1], "primary_button");
      assert.equal(
        stub.lastCall.args[0],
        welcomeScreenWrapper.props().messageId
      );
      assert.ok(stub.lastCall.args[1].includes("text-input-test"));
      assert.equal(stub.lastCall.args[2], "TEXT_INPUT");
      assert.equal(stub.lastCall.args[3].value, "Some user input text");
      stub.restore();
    });

    it("should apply character limit to textarea tile", () => {
      const screens = [
        {
          id: "TEXT_INPUT_TEST",
          content: {
            tiles: {
              type: "textarea",
              data: {
                id: "text-input-test",
                character_limit: 20,
              },
            },
            primary_button: {
              label: "Test Button",
              action: {
                collectTextInput: true,
              },
              disabled: "hasTextInput",
            },
          },
        },
      ];
      const COMPONENT_PROPS = {
        defaultScreens: screens,
        message_id: "DEFAULT_ABOUTWELCOME",
        startScreen: 0,
      };
      let wrapper = mount(<MultiStageAboutWelcome {...COMPONENT_PROPS} />);
      wrapper.update();

      // Input a 10,000 character long string to exceed the character limit
      const textArea = wrapper.find("textarea.textarea-input");
      const value = "A".repeat(21);
      textArea.simulate("change", { target: { value } });
      wrapper.update();

      // Check the char counter, it should show a negative value and be invalid
      const charCounter = wrapper.find(".textarea-char-counter");
      assert.equal(charCounter.text(), "-1");
      assert.ok(charCounter.hasClass("invalid"));

      // The primary button should be disabled due to hasTextInput rule
      const btnPrimary = wrapper.find(".primary");
      assert.ok(btnPrimary.props().disabled);
    });

    it("should truncate long text input values in telemetry ping on collectTextInput", () => {
      const screens = [
        {
          id: "TEXT_INPUT_TEST",
          content: {
            tiles: {
              type: "textarea",
              data: {},
            },
            primary_button: {
              label: "Test Button",
              action: {
                collectTextInput: true,
              },
            },
          },
        },
      ];
      const COMPONENT_PROPS = {
        defaultScreens: screens,
        message_id: "DEFAULT_ABOUTWELCOME",
        startScreen: 0,
      };
      const stub = sinon.stub(MultiStageUtils, "sendActionTelemetry");
      let wrapper = mount(<MultiStageAboutWelcome {...COMPONENT_PROPS} />);
      wrapper.update();

      let welcomeScreenWrapper = wrapper.find(WelcomeScreen);
      // Input a 10,000 character long string to exceed the 8KB limit
      const textArea = welcomeScreenWrapper.find("textarea.textarea-input");
      const value = "A".repeat(10_000);
      textArea.simulate("change", { target: { value } });
      wrapper.update();
      welcomeScreenWrapper = wrapper.find(WelcomeScreen);

      // Record telemetry - this is where the value should be truncated
      const btnPrimary = welcomeScreenWrapper.find(".primary");
      btnPrimary.simulate("click");
      assert.calledTwice(stub);
      assert.equal(
        stub.firstCall.args[0],
        welcomeScreenWrapper.props().messageId
      );
      assert.equal(stub.firstCall.args[1], "primary_button");
      assert.equal(
        stub.lastCall.args[0],
        welcomeScreenWrapper.props().messageId
      );
      // Should generate a tile id automatically if omitted
      assert.ok(stub.lastCall.args[1].includes("tile-0"));
      assert.equal(stub.lastCall.args[2], "TEXT_INPUT");
      assert.equal(stub.lastCall.args[3].value.length, 8192);
      stub.restore();
    });

    it("does not render anything until targeting/filtering resolves (gated first paint)", async () => {
      let resolveTargeting;
      const targetingPromise = new Promise(r => (resolveTargeting = r));
      globals.set("AWEvaluateScreenTargeting", () => targetingPromise);

      const wrapper = mount(
        <MultiStageAboutWelcome {...DEFAULT_PROPS} gateInitialPaint={true} />
      );
      // Immediately after mount (before resolve), nothing should be painted
      assert.strictEqual(wrapper.html(), null);
      assert.ok(!wrapper.find(WelcomeScreen).exists());

      resolveTargeting();
      await spinEventLoop();
      wrapper.update();

      // After targeting resolves, the first screen should render
      assert.ok(wrapper.find(WelcomeScreen).exists());
    });

    it("should not render multi select container if all multiselect item targeting is false", async () => {
      const screens = [
        {
          id: "TEST_SCREEN_FILTERED_TILES",
          content: {
            title: "test title",
            tiles: {
              type: "multiselect",
              data: [
                { id: "checkbox-1", targeting: "false" },
                { id: "checkbox-2", targeting: "false" },
              ],
            },
          },
        },
      ];

      // Simulate the parent targeting pass filtering out every checkbox
      //  which should removes the single multiselect tile entirely
      globals.set("AWEvaluateScreenTargeting", () => {
        const filtered = JSON.parse(JSON.stringify(screens));
        delete filtered[0].content.tiles;
        return Promise.resolve(filtered);
      });

      const wrapper = mount(
        <MultiStageAboutWelcome
          {...DEFAULT_PROPS}
          gateInitialPaint={true}
          defaultScreens={screens}
        />
      );
      await spinEventLoop();
      wrapper.update();

      assert.ok(wrapper.find(WelcomeScreen).exists());
      assert.isFalse(wrapper.find(".multi-select-container").exists());
    });

    it("does not send telemetry before first paint is ready, but does afterwards", async () => {
      let resolveTargeting;
      const targetingPromise = new Promise(r => (resolveTargeting = r));
      globals.set("AWEvaluateScreenTargeting", () => targetingPromise);

      const sendEventStub = sinon.stub(global, "AWSendEventTelemetry");
      const wrapper = mount(
        <MultiStageAboutWelcome {...DEFAULT_PROPS} gateInitialPaint={true} />
      );
      assert.notCalled(sendEventStub);
      assert.strictEqual(wrapper.html(), null);

      resolveTargeting();
      await spinEventLoop();
      wrapper.update();

      const hadImpression = sendEventStub
        .getCalls()
        .some(c => c.args && c.args[0] && c.args[0].event === "IMPRESSION");
      assert.ok(
        hadImpression,
        "Expected at least one IMPRESSION event after paint"
      );

      sendEventStub.restore();
    });

    it("records how a pinnable_sites tile was composed on impression", async () => {
      const screens = [
        {
          id: "TEST_SCREEN_AB",
          content: {
            title: "test title",
            tiles: {
              type: "pinnable_sites",
              data: [
                {
                  id: "site_1",
                  url: "https://a.example.com",
                  personalized: true,
                },
                {
                  id: "site_2",
                  url: "https://b.example.com",
                  personalized: true,
                },
                { id: "curated-gmail", url: "https://mail.google.com" },
              ],
            },
          },
        },
      ];
      const sendEventStub = sinon.stub(global, "AWSendEventTelemetry");

      mount(
        <MultiStageAboutWelcome {...DEFAULT_PROPS} defaultScreens={screens} />
      );
      await spinEventLoop();

      const impression = sendEventStub
        .getCalls()
        .map(c => c.args[0])
        .find(ping => ping?.event === "IMPRESSION");

      assert.equal(
        impression.event_context.personalized_sites,
        2,
        "the impression records how many rows came from the user's history"
      );
      assert.equal(
        impression.event_context.total_sites,
        3,
        "and how many rows there were, so conversion has a per-slot denominator"
      );

      sendEventStub.restore();
    });

    it("fires a screen's impression_action on impression before recording the impression", async () => {
      const impression_action = {
        type: "PIN_FIREFOX_TO_TASKBAR",
        once: true,
      };
      const screens = [
        {
          id: "TEST_SCREEN_AB",
          content: {
            title: "test title",
            impression_action,
          },
        },
      ];
      const handleImpressionStub = sandbox.stub(
        MultiStageUtils,
        "handleImpressionAction"
      );
      const addScreenImpressionStub = globals.set(
        "AWAddScreenImpression",
        sandbox.stub()
      );

      const wrapper = mount(
        <MultiStageAboutWelcome {...DEFAULT_PROPS} defaultScreens={screens} />
      );
      await spinEventLoop();
      wrapper.update();

      assert.calledOnce(handleImpressionStub);
      assert.calledWith(
        handleImpressionStub,
        impression_action,
        sinon.match.string,
        "TEST_SCREEN_AB"
      );
      // The action must fire before the impression is recorded so the parent's
      // `once` check sees the impression count from before this view.
      assert.calledOnce(addScreenImpressionStub);
      assert.ok(
        handleImpressionStub.calledBefore(addScreenImpressionStub),
        "Expected impression action to be dispatched before AWAddScreenImpression"
      );
    });

    it("does not fire an impression_action when a screen doesn't have one", async () => {
      const screens = [
        {
          id: "TEST_SCREEN_NO_ACTION",
          content: { title: "test title" },
        },
      ];
      const handleImpressionStub = sandbox.stub(
        MultiStageUtils,
        "handleImpressionAction"
      );

      const wrapper = mount(
        <MultiStageAboutWelcome {...DEFAULT_PROPS} defaultScreens={screens} />
      );
      await spinEventLoop();
      wrapper.update();

      assert.notCalled(handleImpressionStub);
    });
  });

  describe("MultiStageUtils.handleImpressionAction", () => {
    it("no-ops and sends no telemetry when AWSendImpressionAction is unavailable", async () => {
      // AWSendImpressionAction is only exported on about:welcome. When it is undefined elsewhere
      // the impression action should silently not dispatch without error.
      const telemetryStub = sandbox.stub(
        MultiStageUtils,
        "sendActionTelemetry"
      );

      await MultiStageUtils.handleImpressionAction(
        { type: "PIN_FIREFOX_TO_TASKBAR" },
        "MSG_1",
        "SCREEN_1"
      );

      assert.notCalled(telemetryStub);
    });

    it("dispatches the wrapped action when on about:welcome", () => {
      const action = { type: "PIN_FIREFOX_TO_TASKBAR", once: true };
      const sendStub = globals.set("AWSendImpressionAction", sandbox.stub());

      MultiStageUtils.handleImpressionAction(action, "MSG_1", "SCREEN_1");

      assert.calledOnce(sendStub);
      assert.calledWithMatch(sendStub, {
        action,
        message_id: "MSG_1",
        screen_id: "SCREEN_1",
      });
    });

    it("sends action telemetry when the parent confirms the action fired", async () => {
      const action = { type: "PIN_FIREFOX_TO_TASKBAR" };
      globals.set("AWSendImpressionAction", () => Promise.resolve(true));
      const telemetryStub = sandbox.stub(
        MultiStageUtils,
        "sendActionTelemetry"
      );

      await MultiStageUtils.handleImpressionAction(action, "MSG_1", "SCREEN_1");

      assert.calledOnceWithExactly(
        telemetryStub,
        "MSG_1",
        "PIN_FIREFOX_TO_TASKBAR",
        "IMPRESSION_ACTION"
      );
    });

    it("does not send action telemetry when the action did not fire", async () => {
      globals.set("AWSendImpressionAction", () => Promise.resolve(false));
      const telemetryStub = sandbox.stub(
        MultiStageUtils,
        "sendActionTelemetry"
      );

      await MultiStageUtils.handleImpressionAction(
        { type: "PIN_FIREFOX_TO_TASKBAR" },
        "MSG_1",
        "SCREEN_1"
      );

      assert.notCalled(telemetryStub);
    });
  });

  describe("WelcomeScreen component", () => {
    describe("easy setup screen", () => {
      const easySetupScreen = AboutWelcomeDefaults.getDefaults().screens.find(
        s => s.id === "AW_EASY_SETUP"
      );
      let EASY_SETUP_SCREEN_PROPS;

      beforeEach(() => {
        EASY_SETUP_SCREEN_PROPS = {
          id: easySetupScreen.id,
          content: easySetupScreen.content,
          messageId: `${DEFAULT_PROPS.message_id}_${easySetupScreen.id}`,
          UTMTerm: DEFAULT_PROPS.utm_term,
          flowParams: null,
          totalNumberOfScreens: 1,
          setScreenMultiSelects: sandbox.stub(),
          setActiveMultiSelect: sandbox.stub(),
          setActiveSingleSelectSelection: sandbox.stub(),
        };
      });

      it("should render Easy Setup screen", () => {
        const wrapper = shallow(<WelcomeScreen {...EASY_SETUP_SCREEN_PROPS} />);
        assert.ok(wrapper.exists());
      });

      it("should render a primary and secondary button", () => {
        const wrapper = mount(<WelcomeScreen {...EASY_SETUP_SCREEN_PROPS} />);
        assert.ok(wrapper.find(".primary").exists());
        assert.equal(
          wrapper.find(".secondary-cta:not(.top) button.secondary").length,
          1,
          "One secondary button"
        );
      });

      it("should render secondary.top button", () => {
        let SCREEN_PROPS = {
          content: {
            title: "Step",
            secondary_button_top: {
              text: "test",
              label: "test label",
            },
          },
          position: "top",
        };
        const wrapper = mount(<SecondaryCTA {...SCREEN_PROPS} />);
        assert.ok(wrapper.find("div.secondary-cta.top").exists());
      });

      it("should render a secondary top 'Sign in' button if user isn't signed in", async () => {
        globals.set("AWEvaluateAttributeTargeting", expression => {
          return Promise.resolve(expression.includes("!isFxASignedIn"));
        });

        const wrapper = mount(<WelcomeScreen {...EASY_SETUP_SCREEN_PROPS} />);
        await spinEventLoop();
        wrapper.update();

        const secondaryTopButton = wrapper.find(".secondary-cta.top");
        assert.ok(secondaryTopButton.exists(), "secondary button top exists");

        const signInButton = secondaryTopButton.find(
          '[data-l10n-id="mr1-onboarding-sign-in-button-label"]'
        );

        assert.equal(
          signInButton.length,
          1,
          "One 'Sign in' secondary top button"
        );
      });

      it("should render a secondary top 'Restore Backup' button when backup restore is enabled", async () => {
        globals.set("AWEvaluateAttributeTargeting", expression => {
          return Promise.resolve(expression.includes("backupRestoreEnabled"));
        });

        const wrapper = mount(<WelcomeScreen {...EASY_SETUP_SCREEN_PROPS} />);
        await spinEventLoop();
        wrapper.update();

        const secondaryTopButton = wrapper.find(".secondary-cta.top");
        assert.ok(secondaryTopButton.exists(), "Secondary button top exists");

        let backup = secondaryTopButton.find(
          '[data-l10n-id="restore-from-backup-secondary-top-button"]'
        );

        assert.equal(
          backup.length,
          1,
          "One secondary 'Backup Restore' top button"
        );
      });

      it("should render the arrow icon in the secondary button", () => {
        let SCREEN_PROPS = {
          content: {
            title: "Step",
            secondary_button: {
              has_arrow_icon: true,
              label: "test label",
            },
          },
        };
        const wrapper = mount(<SecondaryCTA {...SCREEN_PROPS} />);
        assert.ok(wrapper.find("button.arrow-icon").exists());
      });

      it("should render steps indicator", () => {
        let PROPS = { totalNumberOfScreens: 1 };
        const wrapper = mount(<StepsIndicator {...PROPS} />);
        assert.ok(wrapper.find("div.indicator").exists());
      });

      it("should assign the total number of screens and current screen to the aria-valuemax and aria-valuenow labels", () => {
        const EXTRA_PROPS = { totalNumberOfScreens: 3, order: 1 };
        const wrapper = mount(
          <WelcomeScreen {...EASY_SETUP_SCREEN_PROPS} {...EXTRA_PROPS} />
        );

        const steps = wrapper.find(`div.steps`);
        assert.ok(steps.exists());
        const { attributes } = steps.getDOMNode();
        assert.equal(
          parseInt(attributes.getNamedItem("aria-valuemax").value, 10),
          EXTRA_PROPS.totalNumberOfScreens
        );
        assert.equal(
          parseInt(attributes.getNamedItem("aria-valuenow").value, 10),
          EXTRA_PROPS.order + 1
        );
      });

      it("should render progress bar", () => {
        let SCREEN_PROPS = {
          step: 1,
          previousStep: 0,
          totalNumberOfScreens: 2,
        };
        const wrapper = mount(<ProgressBar {...SCREEN_PROPS} />);
        assert.ok(wrapper.find("div.indicator").exists());
        assert.propertyVal(
          wrapper.find("div.indicator").prop("style"),
          "--progress-bar-progress",
          "50%"
        );
      });
    });

    describe("theme screen", () => {
      const THEME_SCREEN_PROPS = {
        id: "test-theme-screen",
        totalNumberOfScreens: 1,
        content: {
          title: "test title",
          subtitle: "test subtitle",
          tiles: {
            type: "theme",
            action: {
              theme: "<event>",
            },
            data: [
              {
                theme: "automatic",
                label: "test-label",
                tooltip: "test-tooltip",
                description: "test-description",
              },
            ],
          },
          primary_button: {
            action: {},
            label: "test button",
          },
        },
        navigate: null,
        messageId: `${DEFAULT_PROPS.message_id}_"test-theme-screen"`,
        UTMTerm: DEFAULT_PROPS.utm_term,
        flowParams: null,
        activeTheme: "automatic",
      };

      it("should render WelcomeScreen", () => {
        const wrapper = shallow(<WelcomeScreen {...THEME_SCREEN_PROPS} />);

        assert.ok(wrapper.exists());
      });

      it("should check this.props.activeTheme in the rendered input", () => {
        const wrapper = shallow(<SingleSelect {...THEME_SCREEN_PROPS} />);

        const selectedThemeInput = wrapper.find(".theme input[checked=true]");
        assert.strictEqual(
          selectedThemeInput.prop("value"),
          THEME_SCREEN_PROPS.activeTheme
        );
      });
    });
    describe("import screen", () => {
      const IMPORT_SCREEN_PROPS = {
        content: {
          title: "test title",
          subtitle: "test subtitle",
        },
      };
      it("should render ImportScreen", () => {
        const wrapper = mount(<WelcomeScreen {...IMPORT_SCREEN_PROPS} />);
        assert.ok(wrapper.exists());
      });
      it("should not have a primary or secondary button", () => {
        const wrapper = mount(<WelcomeScreen {...IMPORT_SCREEN_PROPS} />);
        assert.isFalse(wrapper.find(".primary").exists());
        assert.isFalse(
          wrapper.find(".secondary button[value='secondary_button']").exists()
        );
        assert.isFalse(
          wrapper
            .find(".secondary button[value='secondary_button_top']")
            .exists()
        );
      });
    });

    describe("Single select picker screen", () => {
      let SINGLE_SELECT_SCREEN_PROPS;
      beforeEach(() => {
        SINGLE_SELECT_SCREEN_PROPS = {
          content: {
            title: {
              raw: "Test title",
            },
            subtitle: {
              raw: "Test subtitle",
            },
            tiles: {
              type: "single-select",
              selected: "test1",
              action: {
                picker: "<event>",
              },
              data: [
                {
                  id: "test1",
                  label: {
                    raw: "test1 label",
                  },
                  subtitle: {
                    raw: "test1 subtitle",
                  },
                  action: {
                    type: "SET_PREF",
                    data: {
                      pref: {
                        name: "sidebar.revamp",
                        value: true,
                      },
                    },
                  },
                },
                {
                  defaultValue: true,
                  id: "test2",
                  label: {
                    raw: "test2 label",
                  },
                  flair: {
                    text: {
                      raw: "New!",
                    },
                  },
                  action: {
                    type: "SET_PREF",
                    data: {
                      pref: {
                        name: "sidebar.revamp",
                        value: false,
                      },
                    },
                  },
                },
              ],
            },
            secondary_button: {
              label: {
                raw: "Skip this step",
              },
              action: {
                navigate: true,
              },
              has_arrow_icon: true,
            },
          },
          navigate: sandbox.stub(),
          setActiveSingleSelectSelection: sandbox.stub(),
          setActiveMultiSelect: sandbox.stub(),
          setScreenMultiSelects: sandbox.stub(),
        };
        sandbox.stub(MultiStageUtils, "handleUserAction").resolves();
      });
      it("should select the configured default value if present", async () => {
        const wrapper = mount(
          <WelcomeScreen {...SINGLE_SELECT_SCREEN_PROPS} />
        );
        assert.ok(
          wrapper
            .find(".tiles-single-select-section .select-item .test1")
            .exists()
        );
      });
      it("should preselect the active value if present", async () => {
        SINGLE_SELECT_SCREEN_PROPS.activeSingleSelectSelections = {
          "tile-0": ["test2"],
        };
        const wrapper = mount(
          <WelcomeScreen {...SINGLE_SELECT_SCREEN_PROPS} />
        );
        assert.ok(
          wrapper
            .find(".tiles-single-select-section .select-item .test2")
            .exists()
        );
      });
      it("should handle item click", () => {
        const wrapper = mount(
          <WelcomeScreen {...SINGLE_SELECT_SCREEN_PROPS} />
        );
        const selectOption = wrapper.find(
          ".tiles-single-select-section .select-item input[value='test1']"
        );
        selectOption.simulate("click");
        assert.calledOnce(MultiStageUtils.handleUserAction);
      });
      it("should handle item key down selection", () => {
        const wrapper = mount(
          <WelcomeScreen {...SINGLE_SELECT_SCREEN_PROPS} />
        );
        const selectOption = wrapper.find(
          ".tiles-single-select-section .select-item input[value='test1']"
        );
        selectOption.simulate("keydown", { key: "Enter" });
        assert.calledOnce(MultiStageUtils.handleUserAction);
      });
      it("should render flair", () => {
        const wrapper = mount(
          <WelcomeScreen {...SINGLE_SELECT_SCREEN_PROPS} />
        );
        const flair = wrapper.find(".flair");
        assert.ok(flair.exists());
        assert.ok(flair.text() === "New!");
      });
      it("should automatically trigger the selected tile's action for an approved action", () => {
        SINGLE_SELECT_SCREEN_PROPS.content.tiles.autoTrigger = true;
        mount(<WelcomeScreen {...SINGLE_SELECT_SCREEN_PROPS} />);
        assert.calledOnce(MultiStageUtils.handleUserAction);
      });
      it("should not trigger the selected tile's action for an unapproved action", () => {
        SINGLE_SELECT_SCREEN_PROPS.content.tiles.autoTrigger = true;
        SINGLE_SELECT_SCREEN_PROPS.content.tiles.data[0].action = "OPEN_URL";

        mount(<WelcomeScreen {...SINGLE_SELECT_SCREEN_PROPS} />);

        assert.notCalled(MultiStageUtils.handleUserAction);
      });
      it("should not trigger the selected tile's action for an unapproved pref with SET_PREF action", () => {
        SINGLE_SELECT_SCREEN_PROPS.content.tiles.autoTrigger = true;
        SINGLE_SELECT_SCREEN_PROPS.content.tiles.data[0].action.data.pref =
          "unapproved.pref";

        mount(<WelcomeScreen {...SINGLE_SELECT_SCREEN_PROPS} />);

        assert.notCalled(MultiStageUtils.handleUserAction);
      });
      it("should trigger all of the selected tile's actions if MULTI_ACTION is used with SET_PREF and allowed prefs", () => {
        SINGLE_SELECT_SCREEN_PROPS.content.tiles.autoTrigger = true;
        SINGLE_SELECT_SCREEN_PROPS.content.tiles.data[0].action = {
          type: "MULTI_ACTION",
          data: {
            actions: [
              {
                type: "SET_PREF",
                data: {
                  pref: {
                    name: "sidebar.revamp",
                  },
                },
              },
              {
                type: "SET_PREF",
                data: {
                  pref: {
                    name: "sidebar.verticalTabs",
                  },
                },
              },
            ],
          },
        };
        mount(<WelcomeScreen {...SINGLE_SELECT_SCREEN_PROPS} />);
        assert.calledOnce(MultiStageUtils.handleUserAction);
      });
      it("should show subtitle text when tile is selected", () => {
        SINGLE_SELECT_SCREEN_PROPS.activeSingleSelectSelections = {
          "single-select-0": "test1",
        };
        const wrapper = mount(
          <WelcomeScreen {...SINGLE_SELECT_SCREEN_PROPS} />
        );
        assert.ok(
          wrapper.find(".subtitle-text").exists(),
          "Subtitle should render for selected tile"
        );
        assert.equal(wrapper.find(".subtitle-text").text(), "test1 subtitle");
      });
      it("should not show subtitle text when tile is not selected", () => {
        const wrapper = mount(
          <WelcomeScreen {...SINGLE_SELECT_SCREEN_PROPS} />
        );
        assert.isFalse(
          wrapper.find(".subtitle-text").exists(),
          "Subtitle should not render when tile is not selected"
        );
      });
      it("should not trigger any of the selected tile's action if MULTI_ACTION is used with one unallowed pref", () => {
        SINGLE_SELECT_SCREEN_PROPS.content.tiles.autoTrigger = true;
        SINGLE_SELECT_SCREEN_PROPS.content.tiles.data[0].action = {
          type: "MULTI_ACTION",
          data: {
            actions: [
              {
                type: "OPEN_URL",
              },
              {
                type: "SET_PREF",
                data: {
                  pref: "sidebar.verticalTabs",
                },
              },
            ],
          },
        };
        mount(<WelcomeScreen {...SINGLE_SELECT_SCREEN_PROPS} />);
        assert.notCalled(MultiStageUtils.handleUserAction);
      });
    });

    describe("#handleAction", () => {
      let SCREEN_PROPS;
      let TEST_ACTION;
      beforeEach(() => {
        SCREEN_PROPS = {
          content: {
            title: "test title",
            subtitle: "test subtitle",
            primary_button: {
              action: {},
              label: "test button",
            },
          },
          navigate: sandbox.stub(),
          setActiveTheme: sandbox.stub(),
          UTMTerm: "you_tee_emm",
        };
        TEST_ACTION = SCREEN_PROPS.content.primary_button.action;
        sandbox.stub(MultiStageUtils, "handleUserAction").resolves();
      });
      it("should handle navigate", () => {
        TEST_ACTION.navigate = true;
        const wrapper = mount(<WelcomeScreen {...SCREEN_PROPS} />);

        wrapper.find(".primary").simulate("click");

        assert.calledOnce(SCREEN_PROPS.navigate);
      });
      it("should handle theme", () => {
        TEST_ACTION.theme = "test";
        const wrapper = mount(<WelcomeScreen {...SCREEN_PROPS} />);

        wrapper.find(".primary").simulate("click");

        assert.calledWith(SCREEN_PROPS.setActiveTheme, "test");
      });
      it("should handle dismiss", () => {
        SCREEN_PROPS.content.dismiss_button = {
          action: { dismiss: true },
        };
        const finishStub = sandbox.stub(global, "AWFinish");
        const wrapper = mount(<WelcomeScreen {...SCREEN_PROPS} />);
        let telemetrySpy = sandbox.spy(MultiStageUtils, "sendDismissTelemetry");
        wrapper.find(".dismiss-button").simulate("click");

        assert.calledOnce(finishStub);
        assert.calledOnce(telemetrySpy);
      });
      it("should handle SHOW_FIREFOX_ACCOUNTS", () => {
        TEST_ACTION.type = "SHOW_FIREFOX_ACCOUNTS";
        const wrapper = mount(<WelcomeScreen {...SCREEN_PROPS} />);

        wrapper.find(".primary").simulate("click");

        assert.calledWith(MultiStageUtils.handleUserAction, {
          data: {
            extraParams: {
              utm_campaign: "firstrun",
              utm_medium: "referral",
              utm_source: "activity-stream",
              utm_term: "you_tee_emm-screen",
            },
          },
          type: "SHOW_FIREFOX_ACCOUNTS",
        });
      });
      it("should handle OPEN_URL", () => {
        TEST_ACTION.type = "OPEN_URL";
        TEST_ACTION.data = {
          args: "https://example.com?utm_campaign=test-campaign",
        };
        TEST_ACTION.addFlowParams = true;
        let flowBeginTime = Date.now();
        const wrapper = mount(
          <WelcomeScreen
            {...SCREEN_PROPS}
            flowParams={{
              deviceId: "test-device-id",
              flowId: "test-flow-id",
              flowBeginTime,
            }}
          />
        );

        wrapper.find(".primary").simulate("click");

        let [handledAction] = MultiStageUtils.handleUserAction.firstCall.args;
        assert.equal(handledAction.type, "OPEN_URL");
        let { searchParams } = new URL(handledAction.data.args);
        assert.equal(searchParams.get("utm_campaign"), "test-campaign");
        assert.equal(searchParams.get("utm_medium"), "referral");
        assert.equal(searchParams.get("utm_source"), "activity-stream");
        assert.equal(searchParams.get("utm_term"), "you_tee_emm-screen");
        assert.equal(searchParams.get("device_id"), "test-device-id");
        assert.equal(searchParams.get("flow_id"), "test-flow-id");
        assert.equal(
          searchParams.get("flow_begin_time"),
          flowBeginTime.toString()
        );
      });
      it("should handle SHOW_MIGRATION_WIZARD", () => {
        TEST_ACTION.type = "SHOW_MIGRATION_WIZARD";
        const wrapper = mount(<WelcomeScreen {...SCREEN_PROPS} />);

        wrapper.find(".primary").simulate("click");

        assert.calledWith(MultiStageUtils.handleUserAction, {
          type: "SHOW_MIGRATION_WIZARD",
        });
      });
      it("should handle SHOW_MIGRATION_WIZARD INSIDE MULTI_ACTION", async () => {
        const migrationCloseStub = sandbox.stub(
          global,
          "AWWaitForMigrationClose"
        );
        const MULTI_ACTION_SCREEN_PROPS = {
          content: {
            title: "test title",
            subtitle: "test subtitle",
            primary_button: {
              action: {
                type: "MULTI_ACTION",
                navigate: true,
                data: {
                  actions: [
                    {
                      type: "PIN_FIREFOX_TO_TASKBAR",
                    },
                    {
                      type: "SET_DEFAULT_BROWSER",
                    },
                    {
                      type: "SHOW_MIGRATION_WIZARD",
                      data: {},
                    },
                  ],
                },
              },
              label: "test button",
            },
          },
          navigate: sandbox.stub(),
        };
        const wrapper = mount(<WelcomeScreen {...MULTI_ACTION_SCREEN_PROPS} />);

        wrapper.find(".primary").simulate("click");
        assert.calledWith(MultiStageUtils.handleUserAction, {
          type: "MULTI_ACTION",
          navigate: true,
          data: {
            actions: [
              {
                type: "PIN_FIREFOX_TO_TASKBAR",
              },
              {
                type: "SET_DEFAULT_BROWSER",
              },
              {
                type: "SHOW_MIGRATION_WIZARD",
                data: {},
              },
            ],
          },
        });
        // handleUserAction returns a Promise, so let's let the microtask queue
        // flush so that anything waiting for the handleUserAction Promise to
        // resolve can run.
        await new Promise(resolve => queueMicrotask(resolve));
        assert.calledOnce(migrationCloseStub);
      });

      it("should handle SHOW_MIGRATION_WIZARD INSIDE NESTED MULTI_ACTION", async () => {
        const migrationCloseStub = sandbox.stub(
          global,
          "AWWaitForMigrationClose"
        );
        const MULTI_ACTION_SCREEN_PROPS = {
          content: {
            title: "test title",
            subtitle: "test subtitle",
            primary_button: {
              action: {
                type: "MULTI_ACTION",
                navigate: true,
                data: {
                  actions: [
                    {
                      type: "PIN_FIREFOX_TO_TASKBAR",
                    },
                    {
                      type: "SET_DEFAULT_BROWSER",
                    },
                    {
                      type: "MULTI_ACTION",
                      data: {
                        actions: [
                          {
                            type: "SET_PREF",
                          },
                          {
                            type: "SHOW_MIGRATION_WIZARD",
                            data: {},
                          },
                        ],
                      },
                    },
                  ],
                },
              },
              label: "test button",
            },
          },
          navigate: sandbox.stub(),
        };
        const wrapper = mount(<WelcomeScreen {...MULTI_ACTION_SCREEN_PROPS} />);

        wrapper.find(".primary").simulate("click");
        assert.calledWith(MultiStageUtils.handleUserAction, {
          type: "MULTI_ACTION",
          navigate: true,
          data: {
            actions: [
              {
                type: "PIN_FIREFOX_TO_TASKBAR",
              },
              {
                type: "SET_DEFAULT_BROWSER",
              },
              {
                type: "MULTI_ACTION",
                data: {
                  actions: [
                    {
                      type: "SET_PREF",
                    },
                    {
                      type: "SHOW_MIGRATION_WIZARD",
                      data: {},
                    },
                  ],
                },
              },
            ],
          },
        });
        // handleUserAction returns a Promise, so let's let the microtask queue
        // flush so that anything waiting for the handleUserAction Promise to
        // resolve can run.
        await new Promise(resolve => queueMicrotask(resolve));
        assert.calledOnce(migrationCloseStub);
      });
      it("should unset prefs from unchecked checkboxes", () => {
        const PREF_SCREEN_PROPS = {
          content: {
            title: "Checkboxes",
            tiles: {
              type: "multiselect",
              data: [
                {
                  id: "checkbox-1",
                  label: "checkbox 1",
                  checkedAction: {
                    type: "SET_PREF",
                    data: {
                      pref: {
                        name: "pref-a",
                        value: true,
                      },
                    },
                  },
                  uncheckedAction: {
                    type: "SET_PREF",
                    data: {
                      pref: {
                        name: "pref-a",
                      },
                    },
                  },
                },
                {
                  id: "checkbox-2",
                  label: "checkbox 2",
                  checkedAction: {
                    type: "MULTI_ACTION",
                    data: {
                      actions: [
                        {
                          type: "SET_PREF",
                          data: {
                            pref: {
                              name: "pref-b",
                              value: "pref-b",
                            },
                          },
                        },
                        {
                          type: "SET_PREF",
                          data: {
                            pref: {
                              name: "pref-c",
                              value: 3,
                            },
                          },
                        },
                      ],
                    },
                  },
                  uncheckedAction: {
                    type: "SET_PREF",
                    data: {
                      pref: { name: "pref-b" },
                    },
                  },
                },
              ],
            },
            primary_button: {
              label: "Set Prefs",
              action: {
                type: "MULTI_ACTION",
                collectSelect: true,
                isDynamic: true,
                navigate: true,
                data: {
                  actions: [],
                },
              },
            },
          },
          navigate: sandbox.stub(),
          setScreenMultiSelects: sandbox.stub(),
          setActiveMultiSelect: sandbox.stub(),
        };

        // No checkboxes checked. All prefs will be unset and pref-c will not be
        // reset.
        {
          const wrapper = mount(
            <WelcomeScreen {...PREF_SCREEN_PROPS} activeMultiSelect={[]} />
          );
          wrapper.find(".primary").simulate("click");
          assert.calledWith(MultiStageUtils.handleUserAction, {
            type: "MULTI_ACTION",
            collectSelect: true,
            isDynamic: true,
            navigate: true,
            data: {
              actions: [
                { type: "SET_PREF", data: { pref: { name: "pref-a" } } },
                { type: "SET_PREF", data: { pref: { name: "pref-b" } } },
              ],
            },
          });

          MultiStageUtils.handleUserAction.resetHistory();
        }

        // The first checkbox is checked. Only pref-a will be set and pref-c
        // will not be reset.
        {
          const wrapper = mount(
            <WelcomeScreen
              {...PREF_SCREEN_PROPS}
              activeMultiSelect={{ "tile-0": ["checkbox-1"] }}
            />
          );
          wrapper.find(".primary").simulate("click");
          assert.calledWith(MultiStageUtils.handleUserAction, {
            type: "MULTI_ACTION",
            collectSelect: true,
            isDynamic: true,
            navigate: true,
            data: {
              actions: [
                {
                  type: "SET_PREF",
                  data: {
                    pref: {
                      name: "pref-a",
                      value: true,
                    },
                  },
                },
                { type: "SET_PREF", data: { pref: { name: "pref-b" } } },
              ],
            },
          });

          MultiStageUtils.handleUserAction.resetHistory();
        }

        // The second checkbox is checked. Prefs pref-b and pref-c will be set.
        {
          const wrapper = mount(
            <WelcomeScreen
              {...PREF_SCREEN_PROPS}
              activeMultiSelect={{ "tile-0": ["checkbox-2"] }}
            />
          );
          wrapper.find(".primary").simulate("click");
          assert.calledWith(MultiStageUtils.handleUserAction, {
            type: "MULTI_ACTION",
            collectSelect: true,
            isDynamic: true,
            navigate: true,
            data: {
              actions: [
                { type: "SET_PREF", data: { pref: { name: "pref-a" } } },
                {
                  type: "MULTI_ACTION",
                  data: {
                    actions: [
                      {
                        type: "SET_PREF",
                        data: { pref: { name: "pref-b", value: "pref-b" } },
                      },
                      {
                        type: "SET_PREF",
                        data: { pref: { name: "pref-c", value: 3 } },
                      },
                    ],
                  },
                },
              ],
            },
          });

          MultiStageUtils.handleUserAction.resetHistory();
        }

        // // Both checkboxes are checked. All prefs will be set.
        {
          const wrapper = mount(
            <WelcomeScreen
              {...PREF_SCREEN_PROPS}
              activeMultiSelect={{ "tile-0": ["checkbox-1", "checkbox-2"] }}
            />
          );
          wrapper.find(".primary").simulate("click");
          assert.calledWith(MultiStageUtils.handleUserAction, {
            type: "MULTI_ACTION",
            collectSelect: true,
            isDynamic: true,
            navigate: true,
            data: {
              actions: [
                {
                  type: "SET_PREF",
                  data: { pref: { name: "pref-a", value: true } },
                },
                {
                  type: "MULTI_ACTION",
                  data: {
                    actions: [
                      {
                        type: "SET_PREF",
                        data: { pref: { name: "pref-b", value: "pref-b" } },
                      },
                      {
                        type: "SET_PREF",
                        data: { pref: { name: "pref-c", value: 3 } },
                      },
                    ],
                  },
                },
              ],
            },
          });

          MultiStageUtils.handleUserAction.resetHistory();
        }
      });
      it("should handle a campaign action when applicable", async () => {
        let actionSpy = sandbox.spy(MultiStageUtils, "handleCampaignAction");
        let telemetrySpy = sandbox.spy(MultiStageUtils, "sendActionTelemetry");

        globals.set("AWGetUnhandledCampaignAction", () =>
          Promise.resolve("SET_DEFAULT_BROWSER")
        );
        // Return true when "HANDLE_CAMPAIGN_ACTION" is sent to parent
        globals.set("AWSendToParent", () => Promise.resolve(true));
        const screens = [
          {
            content: {
              title: "test title",
            },
          },
        ];
        const TEST_PROPS = {
          defaultScreens: screens,
          message_id: "DEFAULT_ABOUTWELCOME",
          startScreen: 0,
        };
        let wrapper = mount(<MultiStageAboutWelcome {...TEST_PROPS} />);
        await spinEventLoop();
        wrapper.update();
        assert.calledOnce(actionSpy);
        // If campaign is handled, we should send telemetry
        assert.calledOnce(telemetrySpy);
        assert.equal(telemetrySpy.firstCall.args[1], "CAMPAIGN_ACTION");
        globals.restore();
      });
      it("should not handle a campaign action when the action has already been handled", async () => {
        let actionSpy = sandbox.spy(MultiStageUtils, "handleCampaignAction");
        let telemetrySpy = sandbox.spy(MultiStageUtils, "sendActionTelemetry");

        globals.set("AWGetUnhandledCampaignAction", () =>
          Promise.resolve(false)
        );
        const screens = [
          {
            content: {
              title: "test title",
            },
          },
        ];
        const TEST_PROPS = {
          defaultScreens: screens,
          message_id: "DEFAULT_ABOUTWELCOME",
          startScreen: 0,
        };
        let wrapper = mount(<MultiStageAboutWelcome {...TEST_PROPS} />);
        await spinEventLoop();
        wrapper.update();
        assert.notCalled(actionSpy);
        assert.notCalled(telemetrySpy);
        globals.restore();
      });
      it("should not send telemetrty when campaign action handling fails", async () => {
        let actionSpy = sandbox.spy(MultiStageUtils, "handleCampaignAction");
        let telemetrySpy = sandbox.spy(MultiStageUtils, "sendActionTelemetry");

        globals.set("AWGetUnhandledCampaignAction", () =>
          Promise.resolve("SET_DEFAULT_BROWSER")
        );

        // Return undefined when "HANDLE_CAMPAIGN_ACTION" is sent to parent as
        // though "AWPage:HANDLE_CAMPAIGN_ACTION" case did not return true due
        // to failure executing action or the campaign handled pref being true
        globals.set("AWSendToParent", () => Promise.resolve(undefined));
        const screens = [
          {
            content: {
              title: "test title",
            },
          },
        ];
        const TEST_PROPS = {
          defaultScreens: screens,
          message_id: "DEFAULT_ABOUTWELCOME",
          startScreen: 0,
        };
        let wrapper = mount(<MultiStageAboutWelcome {...TEST_PROPS} />);
        await spinEventLoop();
        wrapper.update();
        assert.calledOnce(actionSpy);
        // If campaign handling fails, we should not send telemetry
        assert.notCalled(telemetrySpy);
        globals.restore();
      });
      it("should handle action from matched tile in tiles array", () => {
        const TILE_ID = "test-tile-id";
        SCREEN_PROPS.content.tiles = [
          {
            data: [
              {
                id: TILE_ID,
                action: {
                  type: "OPEN_URL",
                  data: { args: "https://example.com" },
                },
              },
            ],
          },
        ];
        const wrapper = mount(<WelcomeScreen {...SCREEN_PROPS} />);

        wrapper.instance().handleAction({
          currentTarget: { value: TILE_ID },
          source: "test-source",
          name: "test-click",
        });

        const [calledArg] = MultiStageUtils.handleUserAction.firstCall.args;
        const url = new URL(calledArg.data.args);

        assert.equal(calledArg.type, "OPEN_URL");
        assert.equal(url.hostname, "example.com");
      });
    });

    describe("#handleUserAction", () => {
      let SCREEN_PROPS;
      let TEST_ACTION;
      let awSendToParentStub;
      let finishStub;
      let handleUserActionSpy;
      beforeEach(() => {
        SCREEN_PROPS = {
          content: {
            primary_button: {
              action: { type: "TEST_ACTION" },
              label: "test button",
            },
          },
        };
        awSendToParentStub = sandbox.stub();
        globals.set("AWSendToParent", awSendToParentStub);
        finishStub = sandbox.stub(global, "AWFinish");
        handleUserActionSpy = sandbox.spy(MultiStageUtils, "handleUserAction");

        TEST_ACTION = SCREEN_PROPS.content.primary_button.action;
      });

      afterEach(() => {
        handleUserActionSpy.restore();
        finishStub.restore();
        globals.restore();
      });

      it("should dismiss when resolve boolean is true and needAwait true", async () => {
        TEST_ACTION.dismiss = "actionResult";
        TEST_ACTION.needsAwait = true;
        // `needsAwait` is true, so the handleUserAction function should return a `Promise<boolean>`
        awSendToParentStub.callsFake(
          () => Promise.resolve(true) // Resolves to a boolean for awaited calls
        );

        const wrapper = mount(<WelcomeScreen {...SCREEN_PROPS} />);
        wrapper.find(".primary").simulate("click");

        // Assert click of primary button calls handleUserAction
        assert.calledOnce(handleUserActionSpy);
        assert.equal(handleUserActionSpy.firstCall.args[0].type, "TEST_ACTION");
        // handleUserAction returns a Promise, so let's let the microtask queue
        // flush so that anything waiting for the handleUserAction Promise to
        // resolve can run.
        await new Promise(resolve => queueMicrotask(resolve));

        // Check handleUserAction return value is a `Promise<boolean>`
        assert.ok(handleUserActionSpy.firstCall.returnValue instanceof Promise);
        const awaitedResult = await handleUserActionSpy.firstCall.returnValue;
        // Check that the result is a boolean
        assert.equal(
          typeof awaitedResult,
          "boolean",
          "The awaited call should return a boolean."
        );
        assert.equal(
          awaitedResult,
          true,
          "The awaited call should resolve to true, as per the mock return value."
        );
        // Check AWFinish gets called when awaited actionResult value is true
        assert.calledOnce(finishStub);
      });

      it("should not dismiss when resolve boolean is false and needAwait true", async () => {
        TEST_ACTION.dismiss = "actionResult";
        TEST_ACTION.needsAwait = true;
        // `needsAwait` is true, so the handleUserAction function should return a `Promise<boolean>`
        awSendToParentStub.callsFake(
          () => Promise.resolve(false) // Resolves to a boolean for awaited calls
        );

        const wrapper = mount(<WelcomeScreen {...SCREEN_PROPS} />);
        wrapper.find(".primary").simulate("click");

        // Assert click of primary button calls handleUserAction
        assert.calledOnce(handleUserActionSpy);
        assert.equal(handleUserActionSpy.firstCall.args[0].type, "TEST_ACTION");
        await new Promise(resolve => queueMicrotask(resolve));

        // Check handleUserAction return value is a `Promise<boolean>`
        assert.ok(handleUserActionSpy.firstCall.returnValue instanceof Promise);
        const awaitedResult = await handleUserActionSpy.firstCall.returnValue;
        // Check that the result is a boolean
        assert.equal(
          typeof awaitedResult,
          "boolean",
          "The awaited call should return a boolean."
        );
        assert.equal(
          awaitedResult,
          false,
          "The awaited call should resolve to false, as per the mock return value."
        );
        // Check AWFinish not get called when awaited actionResult value is false
        assert.notCalled(finishStub);
      });

      it("should dismiss when true and handleUserAction not awaited", async () => {
        TEST_ACTION.dismiss = true;
        // `needsAwait` is not set, so the handleUserAction function should return a `Promise<undefined>`
        awSendToParentStub.callsFake(
          () => Promise.resolve(undefined) // Resolves to a undefined for non awaited calls
        );

        const wrapper = mount(<WelcomeScreen {...SCREEN_PROPS} />);
        wrapper.find(".primary").simulate("click");

        // Assert click of primary button calls handleUserAction
        assert.calledOnce(handleUserActionSpy);
        assert.equal(handleUserActionSpy.firstCall.args[0].type, "TEST_ACTION");
        await new Promise(resolve => queueMicrotask(resolve));

        // Check handleUserAction return value is a `Promise<undefined>`
        assert.ok(handleUserActionSpy.firstCall.returnValue instanceof Promise);
        const awaitedResult = await handleUserActionSpy.firstCall.returnValue;
        assert.equal(
          awaitedResult,
          undefined,
          "The awaited call should resolve to undefined, as per the mock return value."
        );
        // Check AWFinish gets called when dismiss is true for non awaited calls
        assert.calledOnce(finishStub);
      });
    });
  });
});
