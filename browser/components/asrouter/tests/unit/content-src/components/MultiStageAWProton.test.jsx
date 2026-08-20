import { AboutWelcomeDefaults } from "modules/AboutWelcomeDefaults.sys.mjs";
import {
  MultiStageProtonScreen,
  screenContentShape,
  ProtonScreenActionButtons,
} from "content-src/components/MultiStageProtonScreen";
import { ASRouterScreenUtils } from "modules/ASRouterScreenUtils.sys.mjs";
import { OnboardingMessageProvider } from "modules/OnboardingMessageProvider.sys.mjs";
import { PanelTestProvider } from "modules/PanelTestProvider.sys.mjs";
import { MultiStageUtils } from "content-src/lib/multistage-utils.mjs";
import MultiStageProtonScreenSchema from "../../../../../../../toolkit/components/messaging-system/schemas/MultiStageProtonScreenSchemas/MultiStageProtonScreenSchemas.json";
import React from "react";
import { mount } from "enzyme";
import PropTypes from "prop-types";

describe("MultiStageAboutWelcomeProton module", () => {
  let sandbox;
  let clock;
  beforeEach(() => {
    clock = sinon.useFakeTimers();
    sandbox = sinon.createSandbox();
  });
  afterEach(() => {
    clock.restore();
    sandbox.restore();
  });

  describe("MultiStageAWProton component", () => {
    it("in-tree screen content conforms to the screen propTypes catalog", async () => {
      const consoleErrorStub = sandbox.stub(console, "error");

      const messages = [
        ...(await PanelTestProvider.getMessages()),
        ...(await OnboardingMessageProvider.getUntranslatedMessages()),
      ];

      const screens = messages
        .filter(message => message.content?.template === "multistage")
        .filter(message => Array.isArray(message.content?.screens))
        .flatMap(message => message.content.screens);

      // eslint-disable-next-line no-shadow
      for (const screen of screens) {
        PropTypes.resetWarningCache();
        PropTypes.checkPropTypes(
          {
            content: MultiStageProtonScreen.propTypes.content,
          },
          { content: screen.content },
          "prop",
          "MultiStageProtonScreen"
        );
      }

      const warnings = consoleErrorStub
        .getCalls()
        .map(call => call.args[0])
        .filter(msg => /Failed prop type|Invalid prop/.test(msg));

      assert.deepEqual(
        warnings,
        [],
        `Screen content in PanelTestProvider or OnboardingMessageProvider fails the propTypes catalog (${warnings.length} total warnings): ${warnings.join("\n")}`
      );
    });

    it("screen content propTypes stay in sync with the documentation schema", () => {
      const linkKeys = [
        "here",
        "settings",
        "ios",
        "android",
        "terms_of_use",
        "privacy_notice",
        "learn-more",
        "email_link",
      ];
      const difference = (a, b) => a.filter(key => !b.includes(key));

      const propTypeKeys = difference(
        Object.keys(screenContentShape),
        linkKeys
      );
      const schemaKeys = Object.keys(
        MultiStageProtonScreenSchema.properties.content.properties
      );

      const inPropTypesOnly = difference(propTypeKeys, schemaKeys);
      const inSchemaOnly = difference(schemaKeys, propTypeKeys);

      assert.ok(
        !inPropTypesOnly.length && !inSchemaOnly.length,
        "The content properties documented in MultiStageProtonScreen.jsx " +
          "propTypes and MultiStageProtonScreenSchemas.json have drifted.\n" +
          `In propTypes but missing from schema: [${inPropTypesOnly.join(", ")}]\n` +
          `In schema but missing from propTypes: [${inSchemaOnly.join(", ")}]`
      );
    });

    it("should render MultiStageProton Screen", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          subtitle: "test subtitle",
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
    });

    it("should render secondary section for split positioned screens", () => {
      const SCREEN_PROPS = {
        content: {
          position: "split",
          title: "test title",
          hero_text: "test subtitle",
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.equal(wrapper.find(".welcome-text h1").text(), "test title");
      assert.equal(
        wrapper.find(".section-secondary h1").text(),
        "test subtitle"
      );
      assert.equal(wrapper.find("main").prop("pos"), "split");
    });

    it("should render secondary section with content background for split positioned screens", () => {
      const BACKGROUND_URL =
        "chrome://activity-stream/content/data/content/assets/confetti.svg";
      const SCREEN_PROPS = {
        content: {
          position: "split",
          background: `url(${BACKGROUND_URL}) var(--mr-secondary-position) no-repeat`,
          split_narrow_bkg_position: "10px",
          title: "test title",
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.ok(
        wrapper
          .find("div.section-secondary")
          .prop("style")
          .background.includes("--mr-secondary-position")
      );
      assert.ok(
        wrapper.find("div.section-secondary").prop("style")[
          "--mr-secondary-background-position-y"
        ],
        "10px"
      );
    });

    it("should render with secondary section for split positioned screens", () => {
      const SCREEN_PROPS = {
        content: {
          position: "split",
          title: "test title",
          hero_text: "test subtitle",
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.equal(wrapper.find(".welcome-text h1").text(), "test title");
      assert.equal(
        wrapper.find(".section-secondary h1").text(),
        "test subtitle"
      );
      assert.equal(wrapper.find("main").prop("pos"), "split");
    });

    describe("animation play/pause button", () => {
      const ANIMATED_BG = "url(chrome://example/anim.svg) center / cover";
      const STATIC_BG = "url(chrome://example/static.svg) center / cover";
      const ANIMATED_HERO = "chrome://example/hero-anim.svg";
      const STATIC_HERO = "chrome://example/hero-static.svg";

      it("should not render the toggle when no static fallback is provided", () => {
        const wrapper = mount(
          <MultiStageProtonScreen
            content={{
              position: "split",
              background: ANIMATED_BG,
              hero_image: { url: ANIMATED_HERO },
            }}
          />
        );
        assert.isFalse(
          wrapper.find("button.animation-play-pause-button").exists()
        );
      });

      it("should not render the toggle when only a static background is provided", () => {
        const wrapper = mount(
          <MultiStageProtonScreen
            content={{
              position: "split",
              background_static: STATIC_BG,
            }}
          />
        );
        assert.isFalse(
          wrapper.find("button.animation-play-pause-button").exists()
        );
      });

      it("should not render the toggle when only a static hero_image is provided", () => {
        const wrapper = mount(
          <MultiStageProtonScreen
            content={{
              position: "split",
              hero_image: { static_url: STATIC_HERO },
            }}
          />
        );
        assert.isFalse(
          wrapper.find("button.animation-play-pause-button").exists()
        );
      });

      it("should render the toggle when a static background fallback is provided", () => {
        const wrapper = mount(
          <MultiStageProtonScreen
            content={{
              position: "split",
              background: ANIMATED_BG,
              background_static: STATIC_BG,
            }}
          />
        );
        const button = wrapper.find("button.animation-play-pause-button");
        assert.isTrue(button.exists());
        assert.equal(button.prop("aria-pressed"), false);
        assert.equal(
          button.prop("data-l10n-id"),
          "onboarding-animation-pause-button"
        );
      });

      it("should render the toggle when a static hero_image fallback is provided", () => {
        const wrapper = mount(
          <MultiStageProtonScreen
            content={{
              position: "split",
              hero_image: { url: ANIMATED_HERO, static_url: STATIC_HERO },
            }}
          />
        );
        assert.isTrue(
          wrapper.find("button.animation-play-pause-button").exists()
        );
      });

      it("should swap background and hero_image to static fallbacks when paused", () => {
        const wrapper = mount(
          <MultiStageProtonScreen
            content={{
              position: "split",
              background: ANIMATED_BG,
              background_static: STATIC_BG,
              hero_image: { url: ANIMATED_HERO, static_url: STATIC_HERO },
            }}
            animationsPaused={true}
            toggleAnimationsPaused={() => {}}
          />
        );
        assert.equal(
          wrapper.find("div.section-secondary").prop("style").background,
          STATIC_BG
        );
        assert.equal(wrapper.find(".hero-image img").prop("src"), STATIC_HERO);
        const button = wrapper.find("button.animation-play-pause-button");
        assert.isTrue(button.hasClass("paused"));
        assert.equal(button.prop("aria-pressed"), true);
        assert.equal(
          button.prop("data-l10n-id"),
          "onboarding-animation-play-button"
        );
      });

      it("should use the animated assets when playing", () => {
        const wrapper = mount(
          <MultiStageProtonScreen
            content={{
              position: "split",
              background: ANIMATED_BG,
              background_static: STATIC_BG,
              hero_image: { url: ANIMATED_HERO, static_url: STATIC_HERO },
            }}
            animationsPaused={false}
            toggleAnimationsPaused={() => {}}
          />
        );
        assert.equal(
          wrapper.find("div.section-secondary").prop("style").background,
          ANIMATED_BG
        );
        assert.equal(
          wrapper.find(".hero-image img").prop("src"),
          ANIMATED_HERO
        );
      });

      it("should invoke toggleAnimationsPaused on click", () => {
        const toggle = sinon.stub();
        const wrapper = mount(
          <MultiStageProtonScreen
            content={{
              position: "split",
              background: ANIMATED_BG,
              background_static: STATIC_BG,
            }}
            animationsPaused={false}
            toggleAnimationsPaused={toggle}
          />
        );
        wrapper.find("button.animation-play-pause-button").simulate("click");
        assert.calledOnce(toggle);
      });

      it("should render the toggle inside main-content for center-position screens", () => {
        const wrapper = mount(
          <MultiStageProtonScreen
            content={{
              position: "center",
              background: ANIMATED_BG,
              background_static: STATIC_BG,
              title: "test",
            }}
          />
        );
        assert.isFalse(wrapper.find(".section-secondary").exists());
        assert.isTrue(
          wrapper
            .find(".main-content > button.animation-play-pause-button")
            .exists()
        );
      });
    });

    it("should render with no secondary section for center positioned screens", () => {
      const SCREEN_PROPS = {
        content: {
          position: "center",
          title: "test title",
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.equal(wrapper.find(".section-secondary").exists(), false);
      assert.equal(wrapper.find(".welcome-text h1").text(), "test title");
      assert.equal(wrapper.find("main").prop("pos"), "center");
    });

    it("should render simple hero text if hero_text is a string or object without string_id", () => {
      // test simple hero text with hero_text string
      const STRING_HERO_TEXT_PROPS = {
        content: {
          position: "split",
          hero_text: "Simple hero text string",
        },
      };

      const wrapper = mount(
        <MultiStageProtonScreen {...STRING_HERO_TEXT_PROPS} />
      );
      assert.ok(wrapper.exists());
      assert.equal(
        wrapper.find(".section-secondary h1").text(),
        "Simple hero text string"
      );

      // test with simple hero text with hero_text string_id
      const STRING_ID_HERO_TEXT_PROPS = {
        content: {
          position: "split",
          hero_text: { string_id: "hero-text-id" },
        },
      };
      const stringIdWrapper = mount(
        <MultiStageProtonScreen {...STRING_ID_HERO_TEXT_PROPS} />
      );
      assert.ok(stringIdWrapper.exists());
      assert.equal(
        stringIdWrapper.find(".section-secondary h1").prop("data-l10n-id"),
        "hero-text-id"
      );

      // test that we're not using the hero-text class
      assert.isFalse(
        wrapper.find(".section-secondary .hero-text").exists(),
        "Simple hero text should not use hero-text class"
      );
    });

    it("should render complex hero text if hero text is an object with title property", () => {
      const COMPLEX_HERO_TEXT_PROPS = {
        content: {
          position: "split",
          hero_text: {
            title: "Test title",
          },
        },
      };
      const wrapper = mount(
        <MultiStageProtonScreen {...COMPLEX_HERO_TEXT_PROPS} />
      );
      assert.ok(wrapper.exists());

      assert.isTrue(
        wrapper.find(".section-secondary .hero-text").exists(),
        "Text container should use hero-text class"
      );

      assert.equal(
        wrapper.find(".section-secondary .hero-text h1").text(),
        "Test title"
      );

      assert.isFalse(
        wrapper.find(".section-secondary .hero-text h2").exists(),
        "No subtitle should be rendered"
      );
    });

    it("should render hero text subtitle if both title and subtitle properties are present", () => {
      const HERO_TEXT_WITH_SUBTITLE_PROPS = {
        content: {
          position: "split",
          hero_text: {
            title: "Title text",
            subtitle: "Subtitle text",
          },
        },
      };
      const wrapper = mount(
        <MultiStageProtonScreen {...HERO_TEXT_WITH_SUBTITLE_PROPS} />
      );
      assert.ok(wrapper.exists());

      assert.isTrue(
        wrapper.find(".section-secondary .hero-text").exists(),
        "Complex hero text should use hero-text class"
      );

      assert.equal(
        wrapper.find(".section-secondary .hero-text h1").text(),
        "Title text"
      );

      assert.isTrue(
        wrapper.find(".section-secondary .hero-text h2").exists(),
        "Subtitle should be rendered when provided"
      );
      assert.equal(
        wrapper.find(".section-secondary .hero-text h2").text(),
        "Subtitle text"
      );
    });

    it("should render hero text title and subtitle with localization if string ids are present", () => {
      const LOCALIZED_HERO_TEXT_PROPS = {
        content: {
          position: "split",
          hero_text: {
            title: { string_id: "hero-title-string-id" },
            subtitle: { string_id: "hero-subtitle-string-id" },
          },
        },
      };
      const wrapper = mount(
        <MultiStageProtonScreen {...LOCALIZED_HERO_TEXT_PROPS} />
      );
      assert.ok(wrapper.exists());

      assert.isTrue(
        wrapper.find(".section-secondary .hero-text").exists(),
        "Text container should use hero-text class"
      );

      const titleElement = wrapper.find(".section-secondary .hero-text h1");
      assert.isTrue(titleElement.exists(), "Title element should exist");
      assert.equal(
        titleElement.prop("data-l10n-id"),
        "hero-title-string-id",
        "Title should have correct string ID for localization"
      );

      const subtitleElement = wrapper.find(".section-secondary .hero-text h2");
      assert.isTrue(subtitleElement.exists(), "Subtitle element should exist");
      assert.equal(
        subtitleElement.prop("data-l10n-id"),
        "hero-subtitle-string-id",
        "Subtitle should have correct string ID for localization"
      );
    });

    it("should not render multiple action buttons if an additional button does not exist", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          primary_button: {
            label: "test primary button",
          },
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.isFalse(wrapper.find(".additional-cta").exists());
    });

    it("should render an additional action button with primary styling if no style has been specified", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          primary_button: {
            label: "test primary button",
          },
          additional_button: {
            label: "test additional button",
          },
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.isTrue(wrapper.find(".additional-cta.primary").exists());
    });

    it("should render an additional action button with secondary styling", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          primary_button: {
            label: "test primary button",
          },
          additional_button: {
            label: "test additional button",
            style: "secondary",
          },
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.equal(wrapper.find(".additional-cta.secondary").exists(), true);
    });

    it("should render an additional action button with primary styling", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          primary_button: {
            label: "test primary button",
          },
          additional_button: {
            label: "test additional button",
            style: "primary",
          },
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.equal(wrapper.find(".additional-cta.primary").exists(), true);
    });

    it("should render an additional action with link styling", () => {
      const SCREEN_PROPS = {
        content: {
          position: "split",
          title: "test title",
          primary_button: {
            label: "test primary button",
          },
          additional_button: {
            label: "test additional button",
            style: "link",
          },
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.equal(wrapper.find(".additional-cta.cta-link").exists(), true);
    });

    it("should render an additional button with vertical orientation", () => {
      const SCREEN_PROPS = {
        content: {
          position: "center",
          title: "test title",
          primary_button: {
            label: "test primary button",
          },
          additional_button: {
            label: "test additional button",
            style: "secondary",
            flow: "column",
          },
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.equal(
        wrapper.find(".additional-cta-container[flow='column']").exists(),
        true
      );
    });

    it("should render disabled primary button if activeMultiSelect is in disabled property", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          primary_button: {
            label: "test primary button",
            disabled: "activeMultiSelect",
          },
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.isTrue(wrapper.find("button.primary[disabled]").exists());
    });

    it("should render disabled secondary button if activeMultiSelect is in disabled property", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          secondary_button: {
            label: "test secondary button",
            disabled: "activeMultiSelect",
          },
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.isTrue(wrapper.find("button.secondary[disabled]").exists());
    });

    it("Primary button with disabled: hasActiveMultiSelect property", () => {
      const MULTI_SELECT_SCREEN_PROPS = {
        content: {
          title: "Test MultiSelect",
          tiles: {
            type: "multiselect",
            data: [
              {
                id: "checkbox-1",
                label: "Option 1",
              },
              {
                id: "checkbox-2",
                label: "Option 2",
              },
            ],
          },
          primary_button: {
            label: "Continue",
            disabled: "hasActiveMultiSelect",
            action: {
              navigate: true,
            },
          },
        },
        navigate: null,
        setScreenMultiSelects: sandbox.stub(),
        setActiveMultiSelect: sandbox.stub(),
      };

      it("should be disabled when no checkboxes are selected", () => {
        const wrapper = mount(
          <MultiStageProtonScreen
            {...MULTI_SELECT_SCREEN_PROPS}
            activeMultiSelect={{}}
          />
        );
        const primaryButton = wrapper.find("button.primary");
        assert.isTrue(
          primaryButton.prop("disabled"),
          "disabled when no checkboxes are selected"
        );
      });

      it("should be disabled when activeMultiSelect tile has an empty array", () => {
        const wrapper = mount(
          <MultiStageProtonScreen
            {...MULTI_SELECT_SCREEN_PROPS}
            activeMultiSelect={{ "tile-0": [] }}
          />
        );
        const primaryButton = wrapper.find("button.primary");
        assert.isTrue(
          primaryButton.prop("disabled"),
          "disabled when tile has empty array"
        );
      });

      it("should be enabled when checkboxes are selected", () => {
        const wrapper = mount(
          <MultiStageProtonScreen
            {...MULTI_SELECT_SCREEN_PROPS}
            activeMultiSelect={{ "tile-0": ["checkbox-1"] }}
          />
        );
        const primaryButton = wrapper.find("button.primary");
        assert.isFalse(
          primaryButton.prop("disabled"),
          "enabled when checkboxes are selected"
        );
      });

      it("should be enabled when a checkbox is selected in any tile", () => {
        const wrapper = mount(
          <MultiStageProtonScreen
            {...MULTI_SELECT_SCREEN_PROPS}
            activeMultiSelect={{
              "tile-0": [],
              "tile-1": ["checkbox-2"],
            }}
          />
        );
        const primaryButton = wrapper.find("button.primary");
        assert.isFalse(
          primaryButton.prop("disabled"),
          "Button should be enabled when any tile has selections"
        );
      });
    });

    it("Primary button should be disabled when activeMultiSelect is null", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          primary_button: {
            label: "test primary button",
            disabled: "hasActiveMultiSelect",
          },
        },
        activeMultiSelect: null,
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.isTrue(
        wrapper.find("button.primary[disabled]").exists(),
        "Button is disabled when activeMultiSelect is null"
      );
    });

    it("Primary button should be disabled when activeMultiSelect is undefined", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          primary_button: {
            label: "test primary button",
            disabled: "hasActiveMultiSelect",
          },
        },
        // activeMultiSelect is intentionally not defined
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.isTrue(
        wrapper.find("button.primary[disabled]").exists(),
        "Button is disabled when activeMultiSelect is undefined"
      );
    });

    it("Additional button with disabled: hasActiveMultiSelect property", () => {
      // All of the above should apply to AdditionalCTA as well
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          tiles: {
            type: "multiselect",
            data: [
              {
                id: "checkbox-1",
                label: "Option 1",
              },
              {
                id: "checkbox-2",
                label: "Option 2",
              },
            ],
          },
          additional_button: {
            label: "test additional button",
            disabled: "hasActiveMultiSelect",
          },
        },
        setScreenMultiSelects: sandbox.stub(),
        setActiveMultiSelect: sandbox.stub(),
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.isTrue(
        wrapper.find("button.additional-cta").prop("disabled"),
        "Button is disabled when activeMultiSelect is null"
      );

      // should be enabled when activeMultiSelect has selections
      wrapper.setProps({
        activeMultiSelect: { "tile-0": ["checkbox-1"] },
      });
      wrapper.update();
      assert.isFalse(
        wrapper.find("button.additional-cta").prop("disabled"),
        "enabled when checkboxes are selected"
      );
    });

    it("Primary button should be disabled when activeSingleSelectSelections is undefined", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          primary_button: {
            label: "test primary button",
            disabled: "hasActiveSingleSelect",
          },
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.isTrue(
        wrapper.find("button.primary").prop("disabled"),
        "Button is disabled when activeSingleSelectSelections is undefined"
      );
    });

    it("Primary button should be disabled when activeSingleSelectSelections has no valid selection", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          primary_button: {
            label: "test primary button",
            disabled: "hasActiveSingleSelect",
          },
        },
        activeSingleSelectSelections: { "single-select-0": "none" },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.isTrue(
        wrapper.find("button.primary").prop("disabled"),
        "Button is disabled when selection is 'none'"
      );
    });

    it("Primary button should be enabled when activeSingleSelectSelections has a valid selection", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          primary_button: {
            label: "test primary button",
            disabled: "hasActiveSingleSelect",
          },
        },
        activeSingleSelectSelections: { "single-select-0": "model_1" },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.isFalse(
        wrapper.find("button.primary").prop("disabled"),
        "Button is enabled when a tile is selected"
      );
    });

    it("Primary button should be disabled when pinnedSites is undefined", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          primary_button: {
            label: "test primary button",
            disabled: "hasPinnedSite",
          },
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.isTrue(
        wrapper.find("button.primary").prop("disabled"),
        "Button is disabled when no site has been pinned"
      );
    });

    it("Primary button should be enabled once a site has been pinned", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          primary_button: {
            label: "test primary button",
            disabled: "hasPinnedSite",
          },
        },
        pinnedSites: true,
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.isFalse(
        wrapper.find("button.primary").prop("disabled"),
        "Button is enabled once a site is pinned"
      );
    });

    it("Additional button with disabled: hasTextInput property", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          tiles: {
            type: "textarea",
            data: {
              id: "text-input-test",
              character_limit: 20,
            },
          },
          additional_button: {
            label: "test additional button",
            disabled: "hasTextInput",
          },
        },
        setTextInput: sandbox.stub(),
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.isTrue(
        wrapper.find("button.additional-cta").prop("disabled"),
        "Button is disabled when textInputs is empty"
      );

      // should be enabled when textInputs has input
      wrapper.setProps({
        textInputs: {
          "text-input-test": { value: "Some input", isValid: true },
        },
      });
      wrapper.update();
      assert.isFalse(
        wrapper.find("button.additional-cta").prop("disabled"),
        "enabled when textInputs has input"
      );
    });

    it("should not render a progress bar if there is 1 step", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          progress_bar: true,
        },
        isSingleScreen: true,
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.equal(wrapper.find(".steps.progress-bar").exists(), false);
    });

    it("should not render a steps indicator if steps indicator is force hidden", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
        },
        forceHideStepsIndicator: true,
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.equal(wrapper.find(".steps").exists(), false);
    });

    it("should render title with ref and tabIndex for screens with requireAction prop", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
        },
        requireAction: true,
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());

      const title = wrapper.find("h1#mainContentHeader");
      assert.ok(title.exists());
      assert.equal(title.prop("tabIndex"), -1);
    });

    it("should render a steps indicator above action buttons", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          progress_bar: true,
          primary_button: {},
        },
        aboveButtonStepsIndicator: true,
        totalNumberOfScreens: 2,
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());

      const stepsIndicator = wrapper.find(".steps");
      assert.ok(stepsIndicator, true);

      const stepsDOMNode = stepsIndicator.getDOMNode();
      const siblingElement = stepsDOMNode.nextElementSibling;
      assert.equal(siblingElement.classList.contains("action-buttons"), true);
    });

    it("should render the steps indicator in main inner content if fullscreen and not progress bar style", () => {
      const SCREEN_PROPS = {
        content: {
          title: "Test Fullscreen Dot Steps",
          fullscreen: true,
          position: "split",
          progress_bar: false,
          totalNumberOfScreens: 2,
        },
      };

      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);

      const stepsIndicators = wrapper.find(".steps");
      assert.equal(
        stepsIndicators.length,
        1,
        "Only one steps indicator should be rendered"
      );

      assert.isTrue(
        wrapper.find(".main-content-inner .steps").exists(),
        "Steps indicator is inside main-content-inner"
      );

      assert.isFalse(
        stepsIndicators.first().hasClass("progress-bar"),
        "Steps indicator should not have progress-bar class"
      );
    });

    it("should render a progress bar if there are 2 steps", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          progress_bar: true,
        },
        totalNumberOfScreens: 2,
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.equal(wrapper.find(".steps.progress-bar").exists(), true);
    });

    it("should render confirmation-screen if layout property is set to inline", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          layout: "inline",
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.equal(wrapper.find("[layout='inline']").exists(), true);
    });

    it("should render an inline image with alt text and height property", async () => {
      const SCREEN_PROPS = {
        content: {
          above_button_content: [
            {
              type: "image",
              url: "https://example.com/test.svg",
              height: "auto",
              alt_text: "test alt text",
            },
          ],
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      const imageEl = wrapper.find(".inline-image img");
      assert.equal(imageEl.exists(), true);
      assert.propertyVal(imageEl.prop("style"), "height", "auto");
      const altTextCointainer = wrapper.find(".sr-only");
      assert.equal(altTextCointainer.contains("test alt text"), true);
    });

    it("should render multiple inline elements in correct order", async () => {
      const SCREEN_PROPS = {
        content: {
          above_button_content: [
            {
              type: "image",
              url: "https://example.com/test.svg",
              height: "auto",
              alt_text: "test alt text",
            },
            {
              type: "text",
              text: {
                string_id: "test-string-id",
              },
              link_keys: ["privacy_policy", "terms_of_use"],
            },
            {
              type: "image",
              url: "https://example.com/test_2.svg",
              height: "auto",
              alt_text: "test alt text 2",
            },
            {
              type: "text",
              text: {
                string_id: "test-string-id-2",
              },
              link_keys: ["privacy_policy", "terms_of_use"],
            },
          ],
        },
      };

      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      const imageEl = wrapper.find(".inline-image img");
      const textEl = wrapper.find(".link-paragraph");

      assert.equal(imageEl.length, 2);
      assert.equal(textEl.length, 2);

      assert.equal(imageEl.at(0).prop("src"), "https://example.com/test.svg");
      assert.equal(imageEl.at(1).prop("src"), "https://example.com/test_2.svg");

      assert.equal(textEl.at(0).prop("data-l10n-id"), "test-string-id");
      assert.equal(textEl.at(1).prop("data-l10n-id"), "test-string-id-2");
    });

    it("should render above_button_content legal copy with MultiSelect tile", async () => {
      const SCREEN_PROPS = {
        content: {
          tiles: {
            type: "multiselect",
            label: "Test Subtitle",
            data: [
              {
                id: "checkbox-1",
                type: "checkbox",
                defaultValue: false,
                label: { raw: "Checkbox 1" },
              },
            ],
          },
          above_button_content: [
            {
              type: "text",
              text: {
                string_id: "test-string-id",
              },
              font_styles: "legal",
              link_keys: ["privacy_policy", "terms_of_use"],
            },
          ],
        },
        setScreenMultiSelects: sandbox.stub(),
        setActiveMultiSelect: sandbox.stub(),
      };

      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      const legalText = wrapper.find(".legal-paragraph");
      assert.equal(legalText.exists(), true);

      const multiSelectContainer = wrapper.find(".multi-select-container");
      assert.equal(multiSelectContainer.exists(), true);

      sandbox.restore();
    });

    it("should not have no-rdm property when property is not in message content", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          layout: "inline",
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.notExists(wrapper.find("main").prop("no-rdm"));
    });

    it("should have no-rdm property when property is set in message content", () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          layout: "inline",
          no_rdm: true,
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.exists(wrapper.find("main").prop("no-rdm"));
    });

    it("should correctly set reverse-split prop", () => {
      const SCREEN_PROPS = {
        content: {
          position: "split",
          reverse_split: true,
          title: "test title",
          primary_button: {
            label: "test primary button",
          },
          additional_button: {
            label: "test additional button",
            style: "link",
          },
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.equal(wrapper.find("main").prop("reverse-split"), "");
    });

    it("should render with screen custom styles", async () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          position: "center",
          screen_style: {
            overflow: "auto",
            display: "block",
            padding: "40px 0 0 0",
            width: "800px",
            // disallowed style
            height: "500px",
          },
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.ok(
        (wrapper.find("main").getDOMNode().style.cssText =
          `overflow: ${SCREEN_PROPS.content.screen_style.overflow}; display: ${SCREEN_PROPS.content.screen_style.display})`)
      );
      assert.ok(
        (wrapper.find(".section-main").getDOMNode().style.cssText =
          `padding: ${SCREEN_PROPS.content.screen_style.padding}; width: ${SCREEN_PROPS.content.screen_style.width})`)
      );
    });

    it("should render action buttons above content when configured", async () => {
      const SCREEN_PROPS = {
        content: {
          title: "test title",
          position: "center",
          action_buttons_above_content: "true",
          primary_button: {
            label: "test primary button",
          },
        },
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      const welcomeTextEl = wrapper.find(".welcome-text");
      const secondChild = welcomeTextEl.children().at(1);
      assert.strictEqual(
        secondChild.type(),
        ProtonScreenActionButtons,
        "Second child is ProtonScreenActionButtons"
      );
    });

    it("should render action buttons after tiles by default when no position is configured", async () => {
      const SCREEN_PROPS = {
        content: {
          title: "Test title",
          primary_button: { label: "Confirm and continue" },
          tiles_header: { title: "Title" },
          tiles: {
            type: "multiselect",
            data: [
              { id: "checkbox-1", label: "Option 1" },
              { id: "checkbox-2", label: "Option 2" },
            ],
          },
        },
      };

      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());

      const mainInner = wrapper.find(".main-content-inner");
      const lastChild = mainInner.children().last();
      assert.strictEqual(
        lastChild.type(),
        ProtonScreenActionButtons,
        "Last child is ProtonScreenActionButtons"
      );
    });

    it("should render action buttons after subtitle when configured", async () => {
      const SCREEN_PROPS = {
        content: {
          title: "Test title",
          subtitle: "Test subtitle",
          action_buttons_position: "after_subtitle",
          primary_button: { label: "Get started" },
        },
      };

      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists(), "Screen renders");

      // Find the welcome text container
      const welcomeTextEl = wrapper.find(".welcome-text");
      assert.isTrue(welcomeTextEl.exists(), "Welcome text exists");

      const subtitleEl = welcomeTextEl.find("h2");
      assert.ok(subtitleEl.exists(), "Subtitle exists");
      const nextEl = subtitleEl.getDOMNode().nextElementSibling;
      assert.isTrue(
        nextEl.classList.contains("action-buttons"),
        "Next element is action-buttons"
      );
    });

    it("should render action buttons after supporting content but before tiles when configured", async () => {
      const SCREEN_PROPS = {
        content: {
          title: "Welcome to Firefox",
          action_buttons_position: "after_supporting_content",
          above_button_content: [
            {
              type: "text",
              text: { string_id: "tou-existing-user-spotlight-body" },
              font_styles: "legal",
              link_keys: ["terms-of-use", "privacy-notice", "learn-more"],
            },
          ],
          primary_button: { label: "Confirm and continue" },
          tiles_container: { position: "after_supporting_content" },
          tiles: {
            type: "multiselect",
            data: [
              { id: "checkbox-1", label: "Checkbox 1" },
              { id: "checkbox-2", label: "Checkbox 2" },
            ],
          },
        },
        setScreenMultiSelects: sinon.stub(),
        setActiveMultiSelect: sinon.stub(),
      };

      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());

      const legalParagraphEl = wrapper.find(".legal-paragraph");
      assert.equal(legalParagraphEl.exists(), true, "Legal paragraph renders");

      const nextEl = legalParagraphEl.getDOMNode().nextElementSibling;
      assert.isTrue(
        nextEl.classList.contains("action-buttons"),
        "Next element after legal paragraph should be action buttons"
      );

      const afterButtonsEl = nextEl.nextElementSibling;
      assert.isTrue(
        afterButtonsEl.classList.contains("content-tile"),
        "Next element after action buttons should be content tile"
      );
    });

    it("should render tiles before 'above_button_content' by default", async () => {
      const SCREEN_PROPS = {
        content: {
          title: "Test title",
          primary_button: { label: "Continue" },
          above_button_content: [
            {
              type: "text",
              text: { string_id: "tou-existing-user-spotlight-body" },
              font_styles: "legal",
              link_keys: ["terms-of-use", "privacy-notice", "learn-more"],
            },
          ],
          tiles: {
            type: "multiselect",
            data: [
              { id: "checkbox-1", label: "Checkbox 1" },
              { id: "checkbox-2", label: "Checkbox 2" },
            ],
          },
        },
        setScreenMultiSelects: sinon.stub(),
        setActiveMultiSelect: sinon.stub(),
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());

      const tilesEl = wrapper.find(".content-tile");
      assert.isTrue(tilesEl.exists(), "Content tiles found");

      const legalParagraphEl = wrapper.find(".legal-paragraph");
      assert.isTrue(
        legalParagraphEl.exists(),
        "above_button_content legal paragraph element found"
      );

      assert.isTrue(
        tilesEl
          .getDOMNode()
          .nextElementSibling.classList.contains("legal-paragraph"),
        "the next element after tiles should be legal paragraph (above_button_content)"
      );
    });

    it("should render tiles after 'above_button_content' when configured", async () => {
      const SCREEN_PROPS = {
        content: {
          title: "Test title",
          primary_button: { label: "Continue" },
          above_button_content: [
            {
              type: "text",
              text: { string_id: "tou-existing-user-spotlight-body" },
              font_styles: "legal",
              link_keys: ["terms-of-use", "privacy-notice", "learn-more"],
            },
          ],
          tiles_container: { position: "after_supporting_content" },
          tiles: {
            type: "multiselect",
            data: [
              { id: "checkbox-1", label: "Checkbox 1" },
              { id: "checkbox-2", label: "Checkbox 2" },
            ],
          },
        },
        setScreenMultiSelects: sinon.stub(),
        setActiveMultiSelect: sinon.stub(),
      };
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());

      const tilesEl = wrapper.find(".content-tile");
      assert.isTrue(tilesEl.exists(), "Content tiles found");

      const legalParagraphEl = wrapper.find(".legal-paragraph");
      assert.isTrue(
        legalParagraphEl.exists(),
        "above_button_content legal paragraph element found"
      );

      assert.isTrue(
        legalParagraphEl
          .getDOMNode()
          .nextElementSibling.classList.contains("content-tile"),
        "the next element after legal paragraph (above_button_content) should be tiles"
      );
    });
  });

  describe("AboutWelcomeDefaults for proton", () => {
    const getData = () => AboutWelcomeDefaults.getDefaults();

    async function prepConfig(config, evalFalseScreenIds) {
      let data = await getData();

      if (evalFalseScreenIds?.length) {
        // eslint-disable-next-line no-shadow
        data.screens.forEach(async screen => {
          if (evalFalseScreenIds.includes(screen.id)) {
            screen.targeting = false;
          }
        });
        data.screens =
          await ASRouterScreenUtils.evaluateTargetingAndRemoveScreens(
            data.screens
          );
      }

      return AboutWelcomeDefaults.prepareContentForReact({
        ...data,
        ...config,
      });
    }
    beforeEach(() => {
      sandbox.stub(global.Services.prefs, "getBoolPref").returns(true);
      sandbox
        .stub(ASRouterScreenUtils, "evaluateScreenTargeting")
        .returnsArg(0);
      // This is necessary because there are still screens being removed with
      // `removeScreens` in `prepareContentForReact()`. Once we've migrated
      // to using screen targeting instead of manually removing screens,
      // we can remove this stub.
      sandbox
        .stub(global.ASRouterScreenUtils, "removeScreens")
        .callsFake((screens, callback) =>
          ASRouterScreenUtils.removeScreens(screens, callback)
        );
    });
    it("should have a multi action primary button by default", async () => {
      const data = await prepConfig({}, [
        "AW_WELCOME_BACK",
        "RETURN_TO_AMO",
        "AW_BACKUP_RESTORE_EMBEDDED_BACKUP_FOUND",
        "AW_BACKUP_RESTORE_EMBEDDED_MULTIPLE_BACKUPS_FOUND",
      ]);
      assert.propertyVal(
        data.screens[0].content.primary_button.action,
        "type",
        "MULTI_ACTION"
      );
    });
    it("should have a FxA button", async () => {
      const data = await prepConfig({}, [
        "AW_WELCOME_BACK",
        "AW_BACKUP_RESTORE_EMBEDDED_BACKUP_FOUND",
        "AW_BACKUP_RESTORE_EMBEDDED_MULTIPLE_BACKUPS_FOUND",
      ]);

      assert.notProperty(data, "skipFxA");
      assert.property(data.screens[0].content, "secondary_button_top");
    });
    it("should remove the FxA button if pref disabled", async () => {
      global.Services.prefs.getBoolPref.returns(false);

      const data = await prepConfig();

      assert.property(data, "skipFxA", true);
      assert.notProperty(data.screens[0].content, "secondary_button_top");
    });
  });

  describe("AboutWelcomeDefaults for MR split template proton", () => {
    const getData = () => AboutWelcomeDefaults.getDefaults(true);
    beforeEach(() => {
      sandbox.stub(global.Services.prefs, "getBoolPref").returns(true);
    });

    it("should use 'split' position template by default", async () => {
      const data = await getData();
      assert.propertyVal(data.screens[0].content, "position", "split");
    });

    it("should not include noodles by default", async () => {
      const data = await getData();
      assert.notProperty(data.screens[0].content, "has_noodles");
    });
  });

  describe("AboutWelcomeDefaults prepareMobileDownload", () => {
    let listeners = {};
    let mediaQueryListMock;

    beforeEach(() => {
      listeners = {};
      mediaQueryListMock = {
        matches: false,
        media: "(min-width: 800px)",
        addEventListener: (event, cb) => {
          listeners[event] = cb;
        },
        removeEventListener: event => {
          delete listeners[event];
        },
        dispatchEvent: event => {
          if (listeners[event.type]) {
            listeners[event.type](event);
          }
        },
      };

      window.matchMedia = () => mediaQueryListMock;
    });

    it("responds to media query changes and uses main_content_style on wide screens, main_content_style_narrow on narrow screens", () => {
      // narrow screen
      mediaQueryListMock.matches = false;

      const content = {
        main_content_style: { paddingInline: "30px" },
        main_content_style_narrow: { paddingInline: "10px" },
      };

      const wrapper = mount(<MultiStageProtonScreen content={content} />);

      let styleProp = wrapper.find(".main-content-inner").prop("style");
      assert.equal(styleProp.paddingInline, "10px");

      mediaQueryListMock.matches = true;
      mediaQueryListMock.dispatchEvent({ type: "change", matches: true });

      wrapper.update();

      // wide styles should be applied
      styleProp = wrapper.find(".main-content-inner").prop("style");
      assert.equal(styleProp.paddingInline, "30px");
    });
  });

  describe("AboutWelcomeDefaults prepareContentForReact", () => {
    it("should not set action without screens", async () => {
      const data = await AboutWelcomeDefaults.prepareContentForReact({
        ua: "test",
      });

      assert.propertyVal(data, "ua", "test");
      assert.notProperty(data, "screens");
    });
    it("should set action for import action", async () => {
      const TEST_CONTENT = {
        ua: "test",
        screens: [
          {
            id: "AW_IMPORT_SETTINGS",
            content: {
              primary_button: {
                action: {
                  type: "SHOW_MIGRATION_WIZARD",
                },
              },
            },
          },
        ],
      };
      const data =
        await AboutWelcomeDefaults.prepareContentForReact(TEST_CONTENT);
      assert.propertyVal(data, "ua", "test");
      assert.propertyVal(
        data.screens[0].content.primary_button.action.data,
        "source",
        "test"
      );
    });
    it("should not set action if the action type != SHOW_MIGRATION_WIZARD", async () => {
      const TEST_CONTENT = {
        ua: "test",
        screens: [
          {
            id: "AW_IMPORT_SETTINGS",
            content: {
              primary_button: {
                action: {
                  type: "SHOW_FIREFOX_ACCOUNTS",
                  data: {},
                },
              },
            },
          },
        ],
      };
      const data =
        await AboutWelcomeDefaults.prepareContentForReact(TEST_CONTENT);
      assert.propertyVal(data, "ua", "test");
      assert.notPropertyVal(
        data.screens[0].content.primary_button.action.data,
        "source",
        "test"
      );
    });
  });

  describe("Embedded Migration Wizard", () => {
    const SCREEN_PROPS = {
      content: {
        title: "test title",
        tiles: {
          type: "migration-wizard",
        },
      },
      setScreenMultiSelects: sinon.stub(),
      setActiveMultiSelect: sinon.stub(),
    };

    it("should render migration wizard", async () => {
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.isTrue(wrapper.find("migration-wizard").exists());
    });
  });

  describe("Custom main content inner custom justify content", () => {
    const SCREEN_PROPS = {
      content: {
        title: "test title",
        position: "split",
        split_content_justify_content: "flex-start",
      },
    };

    it("should render split screen with custom justify-content", async () => {
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.equal(wrapper.find("main").prop("pos"), "split");
      assert.exists(wrapper.find(".main-content-inner"));
      assert.ok(
        wrapper
          .find(".main-content-inner")
          .prop("style")
          .justifyContent.includes("flex-start")
      );
    });
  });

  describe("Custom content tiles container styles", () => {
    const SCREEN_PROP = {
      content: {
        title: "test title",
        tiles_container: {
          style: {
            flexDirection: "row",
            marginBlock: "16px",
            // disallowed style
            backgroundColor: "blue",
          },
        },
        tiles: [
          {
            type: "multiselect",
            title: {
              raw: "Text 1",
            },
            data: [
              {
                id: "checkbox-1",
                defaultValue: false,
                label: {
                  raw: "Checkbox",
                },
              },
            ],
          },
          {
            type: "multiselect",
            title: {
              raw: "Text 2",
            },
            data: [
              {
                id: "checkbox-1",
                defaultValue: false,
                label: {
                  raw: "Checkbox",
                },
              },
            ],
          },
        ],
      },
      setScreenMultiSelects: sinon.stub(),
      setActiveMultiSelect: sinon.stub(),
    };

    it("should render container with custom styles", async () => {
      const wrapper = mount(
        <MultiStageProtonScreen {...SCREEN_PROP} activeMultiSelect={{}} />
      );
      assert.ok(wrapper.exists());
      const expectedStyles = "flex-direction: row; margin-block: 16px;";
      assert.strictEqual(
        wrapper.find("#content-tiles-container").getDOMNode().style.cssText,
        expectedStyles
      );
    });
  });

  describe("Multiple secondary_top buttons", () => {
    const SCREEN_PROPS = {
      content: {
        title: "test title",
        tiles: {
          type: "migration-wizard",
        },
        secondary_button_top: [
          {
            label: {
              raw: "test button 1",
            },
            action: {
              navigate: true,
            },
          },
          {
            label: {
              raw: "test button 2",
            },
            action: {
              navigate: true,
            },
          },
        ],
      },
      setScreenMultiSelects: sinon.stub(),
      setActiveMultiSelect: sinon.stub(),
    };

    it("should render both buttons in a container", async () => {
      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());
      assert.isTrue(wrapper.find(".secondary-buttons-top-container").exists());
      assert.isTrue(wrapper.find("#secondary_button_0").exists());
      assert.isTrue(wrapper.find("#secondary_button_1").exists());
    });
  });

  describe("Gating on experiment load", () => {
    let SCREEN_PROPS;
    it("should advance after minDisplayMs once experiments gate resolves", async () => {
      const navigateStub = sandbox.stub();

      const awWaitStub = sandbox
        .stub(global, "AWWaitForNimbus")
        .resolves("ready");

      SCREEN_PROPS = {
        content: {
          title: "Loading screen",
        },
        autoAdvance: null,
        advanceOnExperimentLoad: {
          minDisplayMs: 1000,
          maxDisplayMs: 5000,
        },
        navigate: navigateStub,
      };

      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());

      sinon.assert.calledOnce(awWaitStub);

      clock.tick(999);
      await Promise.resolve();
      sinon.assert.notCalled(navigateStub);

      clock.tick(1);
      await Promise.resolve();
      sinon.assert.calledOnce(navigateStub);
      sinon.assert.calledWithExactly(navigateStub, false);

      wrapper.unmount();
    });

    it("should force advance at maxDisplayMs even if Nimbus never resolves", async () => {
      const navigateStub = sandbox.stub();

      sandbox.stub(global, "AWWaitForNimbus").returns(new Promise(() => {}));

      SCREEN_PROPS = {
        content: {
          title: "Loading screen",
        },
        autoAdvance: null,
        advanceOnExperimentLoad: {
          minDisplayMs: 1000,
          maxDisplayMs: 2000,
        },
        navigate: navigateStub,
      };

      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());

      clock.tick(1999);
      await Promise.resolve();
      sinon.assert.notCalled(navigateStub);

      clock.tick(1);
      await Promise.resolve();
      sinon.assert.calledOnce(navigateStub);
      sinon.assert.calledWithExactly(navigateStub, false);

      wrapper.unmount();
    });

    it("should prefer legacy autoAdvance over experiments gate when both are set", async () => {
      const handleActionStub = sandbox.stub();
      const navigateStub = sandbox.stub();

      const awWaitStub = sandbox.stub(global, "AWWaitForNimbus");

      SCREEN_PROPS = {
        content: {
          title: "Legacy auto advance",
        },
        autoAdvance: "SOME_TARGET",
        advanceOnExperimentLoad: {
          minDisplayMs: 1000,
          maxDisplayMs: 2000,
        },
        handleAction: handleActionStub,
        navigate: navigateStub,
      };

      const wrapper = mount(<MultiStageProtonScreen {...SCREEN_PROPS} />);
      assert.ok(wrapper.exists());

      clock.tick(19999);
      await Promise.resolve();
      sinon.assert.notCalled(handleActionStub);
      sinon.assert.notCalled(navigateStub);

      clock.tick(1);
      await Promise.resolve();

      sinon.assert.calledOnce(handleActionStub);
      const [call] = handleActionStub.getCall(0).args;
      assert.propertyVal(call, "name", "AUTO_ADVANCE");
      assert.propertyVal(call.currentTarget, "value", "SOME_TARGET");

      sinon.assert.notCalled(awWaitStub);

      wrapper.unmount();
    });

    it("should send telemetry with reason nimbus_ready when Nimbus resolves before minDisplayMs", async () => {
      const navigateStub = sandbox.stub();
      const telemetryStub = sandbox.stub(
        MultiStageUtils,
        "sendActionTelemetry"
      );
      sandbox.stub(global, "AWWaitForNimbus").resolves("ready");

      const wrapper = mount(
        <MultiStageProtonScreen
          content={{ title: "Loading screen" }}
          autoAdvance={null}
          advanceOnExperimentLoad={{ minDisplayMs: 1000, maxDisplayMs: 5000 }}
          messageId="test-splash-screen"
          navigate={navigateStub}
        />
      );

      clock.tick(999);
      await Promise.resolve();
      sinon.assert.notCalled(telemetryStub);

      clock.tick(1);
      await Promise.resolve();

      sinon.assert.calledOnce(telemetryStub);
      sinon.assert.calledWith(
        telemetryStub,
        "test-splash-screen",
        "advance_on_experiment_load",
        "SPLASH_DISMISSED",
        sinon.match({ reason: "nimbus_ready", screen_duration: 1000 })
      );

      wrapper.unmount();
    });

    it("should send telemetry with reason max_display_timeout when max timer fires before Nimbus", async () => {
      const navigateStub = sandbox.stub();
      const telemetryStub = sandbox.stub(
        MultiStageUtils,
        "sendActionTelemetry"
      );
      sandbox.stub(global, "AWWaitForNimbus").returns(new Promise(() => {}));

      const wrapper = mount(
        <MultiStageProtonScreen
          content={{ title: "Loading screen" }}
          autoAdvance={null}
          advanceOnExperimentLoad={{ minDisplayMs: 1000, maxDisplayMs: 2000 }}
          messageId="test-splash-screen"
          navigate={navigateStub}
        />
      );

      clock.tick(1999);
      await Promise.resolve();
      sinon.assert.notCalled(telemetryStub);

      clock.tick(1);
      await Promise.resolve();

      sinon.assert.calledOnce(telemetryStub);
      sinon.assert.calledWith(
        telemetryStub,
        "test-splash-screen",
        "advance_on_experiment_load",
        "SPLASH_DISMISSED",
        sinon.match({ reason: "max_display_timeout", screen_duration: 2000 })
      );

      wrapper.unmount();
    });

    it("should send telemetry with reason nimbus_error when Nimbus resolves with error", async () => {
      const navigateStub = sandbox.stub();
      const telemetryStub = sandbox.stub(
        MultiStageUtils,
        "sendActionTelemetry"
      );
      sandbox.stub(global, "AWWaitForNimbus").resolves("error");

      const wrapper = mount(
        <MultiStageProtonScreen
          content={{ title: "Loading screen" }}
          autoAdvance={null}
          advanceOnExperimentLoad={{ minDisplayMs: 1000, maxDisplayMs: 5000 }}
          messageId="test-splash-screen"
          navigate={navigateStub}
        />
      );

      clock.tick(999);
      await Promise.resolve();
      sinon.assert.notCalled(telemetryStub);

      clock.tick(1);
      await Promise.resolve();

      sinon.assert.calledOnce(telemetryStub);
      sinon.assert.calledWith(
        telemetryStub,
        "test-splash-screen",
        "advance_on_experiment_load",
        "SPLASH_DISMISSED",
        sinon.match({ reason: "nimbus_error" })
      );

      wrapper.unmount();
    });
  });
});
