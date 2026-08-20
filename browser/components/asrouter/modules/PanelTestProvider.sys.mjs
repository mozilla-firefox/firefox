/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// We use importESModule here instead of static import so that
// the Karma test environment won't choke on this module. This
// is because the Karma test environment already stubs out
// AppConstants, and overrides importESModule to be a no-op (which
// can't be done for a static import statement).

// eslint-disable-next-line mozilla/use-static-import
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

const TWO_DAYS = 2 * 24 * 3600 * 1000;
const isMSIX =
  AppConstants.platform === "win" &&
  Services.sysinfo.getProperty("hasWinPackageId", false);

const MESSAGES = () => [
  {
    weight: 100,
    id: "FEATURE_CALLOUT_REFERRAL_TEST",
    template: "feature_callout",
    description: "Test referral code generation from a message",
    content: {
      id: "FEATURE_CALLOUT_REFERRAL_TEST",
      template: "multistage",
      backdrop: "transparent",
      transitions: false,
      disableHistoryUpdates: true,
      metrics: "block",
      screens: [
        {
          id: "FEATURE_CALLOUT_REFERRAL_TEST",
          anchors: [
            {
              selector: "#PanelUI-menu-button",
              panel_position: {
                anchor_attachment: "bottomcenter",
                callout_attachment: "topright",
              },
            },
          ],
          content: {
            position: "callout",
            title: {
              raw: "This callout will link to about:referrals",
              marginInline: "0 42px",
            },
            above_button_content: [
              {
                type: "text",
                text: [
                  "Click ",
                  {
                    raw: "here",
                    link_key: "here",
                  },
                  " to generate a referral code.",
                ],
                textAlign: "start",
                fontSize: "0.8125em",
                marginBlock: "0",
              },
            ],
            here: {
              action: {
                type: "GET_REFERRAL_CODE",
                data: {
                  entrypoint: "test",
                  where: "tab",
                },
              },
            },
            dismiss_button: {
              action: {
                dismiss: true,
              },
            },
          },
        },
      ],
    },
    targeting: "providerCohorts.panel_local_testing == 'SHOW_TEST'",
    groups: [],
    provider: "panel_local_testing",
  },
  {
    weight: 100,
    id: "FEATURE_CALLOUT_EMBEDDED_LINKS_TEST",
    template: "feature_callout",
    description: "Test embedded links in above_button_content paragraphs",
    content: {
      id: "FEATURE_CALLOUT_EMBEDDED_LINKS_TEST",
      template: "multistage",
      backdrop: "transparent",
      transitions: false,
      disableHistoryUpdates: true,
      metrics: "block",
      screens: [
        {
          id: "FEATURE_CALLOUT_EMBEDDED_LINKS_TEST",
          anchors: [
            {
              selector: "#PanelUI-menu-button",
              panel_position: {
                anchor_attachment: "bottomcenter",
                callout_attachment: "topright",
              },
            },
          ],
          content: {
            position: "callout",
            title: {
              raw: "I'm an example of embedded links",
              marginInline: "0 42px",
            },
            above_button_content: [
              {
                type: "text",
                text: [
                  "Read the release notes ",
                  {
                    raw: {
                      $l10n: {
                        id: "id",
                        text: "here",
                        comment: "text",
                      },
                    },
                    href: "https://www.mozilla.org/en-US/firefox/releases/",
                    where: "tabshifted",
                  },
                  ", or open ",
                  {
                    raw: "settings",
                    link_key: "settings",
                  },
                  ".",
                ],
                textAlign: "start",
                fontSize: "0.8125em",
                marginBlock: "0",
              },
            ],
            settings: {
              action: {
                type: "OPEN_ABOUT_PAGE",
                data: {
                  args: "preferences",
                  where: "tab",
                },
              },
            },
            dismiss_button: {
              action: {
                dismiss: true,
              },
            },
          },
        },
      ],
    },
    targeting: 'providerCohorts.panel_local_testing == "SHOW_TEST"',
    groups: [],
    provider: "panel_local_testing",
  },
  {
    id: "WRITE_IN_MICROSURVEY_TEST",
    template: "feature_callout",
    groups: [],
    content: {
      id: "WRITE_IN_MICROSURVEY_TEST",
      template: "multistage",
      backdrop: "transparent",
      transitions: false,
      write_in_microsurvey: true,
      screens: [
        {
          id: "WRITE_IN_MICROSURVEY_TEST_SCREEN_1",
          force_hide_steps_indicator: true,
          anchors: [
            {
              selector: "hbox#browser",
              no_open_on_anchor: true,
              hide_arrow: true,
              panel_position: {
                anchor_attachment: "bottomright",
                callout_attachment: "bottomright",
                offset_y: -24,
                offset_x: -20,
              },
            },
          ],
          content: {
            position: "callout",
            width: "312px",
            padding: 24,
            title_logo: {
              imageURL: "chrome://branding/content/about-logo.png",
              alignment: "top",
            },
            title: {
              raw: "Help Firefox improve this feature",
            },
            subtitle: {
              raw: "Is there anything you'd like to add? (optional)",
              fontSize: "0.9375em",
            },
            tiles: {
              type: "textarea",
              data: {
                id: "feature-feedback",
                character_limit: 1000,
                rows: 4,
              },
            },
            above_button_content: [
              {
                type: "text",
                text: {
                  raw: "Note: Do not include personal information.",
                  color: "var(--text-color-deemphasized)",
                  fontSize: "0.6875em",
                  textAlign: "start",
                  marginBlock: "-4px",
                },
              },
            ],
            secondary_button: {
              label: { raw: "Submit" },
              style: "primary",
              action: {
                type: "MULTI_ACTION",
                collectTextInput: true,
                data: { actions: [] },
              },
              disabled: "hasTextInput",
            },
            additional_button: {
              label: { raw: "Privacy notice" },
              style: "link",
              alignment: "space-between",
              action: {
                data: {
                  args: "https://www.mozilla.org/privacy/firefox/",
                  where: "tabshifted",
                },
                type: "OPEN_URL",
              },
            },
            dismiss_button: {
              action: {
                dismiss: true,
              },
              background: true,
              size: "small",
              marginInline: "0 20px",
              marginBlock: "20px 0",
            },
          },
        },
      ],
    },
  },
  {
    id: "TASKBAR_TAB_TEST_CALLOUT",
    template: "feature_callout",
    groups: ["cfr"],
    content: {
      id: "TASKBAR_TAB_TEST_CALLOUT",
      template: "multistage",
      backdrop: "transparent",
      transitions: false,
      screens: [
        {
          id: "TASKBAR_TAB_TEST_CALLOUT",
          anchors: [
            {
              selector: "#taskbar-tabs-button",
              panel_position: {
                anchor_attachment: "bottomcenter",
                callout_attachment: "topright",
              },
            },
          ],
          content: {
            position: "callout",
            padding: 16,
            width: "330px",
            title_logo: {
              imageURL:
                "chrome://browser/content/asrouter/assets/smiling-fox-icon.svg",
              width: "24px",
              height: "24px",
              marginInline: "0 16px",
              alignment: "top",
            },
            title: {
              raw: "Taskbar tabs action test",
            },
            primary_button: {
              label: {
                raw: "Create taskbar tab",
              },
              action: {
                type: "CREATE_TASKBAR_TAB",
              },
            },
            dismiss_button: {
              action: {
                dismiss: true,
              },
              background: true,
              size: "small",
              marginInline: "0 20px",
              marginBlock: "20px 0",
            },
          },
        },
      ],
    },
    targeting: "visitsCount >= 1",
    trigger: {
      id: "openURL",
      params: ["wikipedia.org", "www.wikipedia.org"],
    },
    frequency: {
      lifetime: 1,
    },
    skip_in_tests: "it's not tested in automation",
  },
  {
    id: "FREQUENCY_CAP_CLEANUP_TEST",
    profileScope: "single",
    template: "feature_callout",
    groups: [],
    content: {
      id: "FREQUENCY_CAP_CLEANUP_TEST",
      padding: "16",
      template: "multistage",
      backdrop: "transparent",
      transitions: false,
      disableHistoryUpdates: true,
      screens: [
        {
          id: "FREQUENCY_CAP_CLEANUP_TEST",
          anchors: [
            {
              selector: "#fxa-toolbar-menu-button",
              panel_position: {
                anchor_attachment: "bottomcenter",
                callout_attachment: "topright",
              },
            },
          ],
          content: {
            position: "callout",
            width: "400px",
            padding: 16,
            title: {
              raw: "Frequency Cap Test",
            },
            subtitle: {
              raw: "This callout has a very short frequency cap.",
            },
            additional_button: {
              action: {
                dismiss: true,
              },
              label: {
                string_id: "dismiss-button-label",
                fontWeight: "590",
                fontSize: "11px",
              },
              style: "secondary",
            },
          },
        },
      ],
    },
    frequency: {
      custom: [
        {
          cap: 1,
          period: 120000,
        },
      ],
    },
    trigger: {
      id: "defaultBrowserCheck",
    },
    targeting:
      "(region in ['CA', 'US']) && previousSessionEnd && !willShowDefaultPrompt && !activeNotifications && userPrefs.cfrFeatures",
    skip_in_tests: "it's not tested in automation",
  },
  {
    id: "CONTENT_TILES_TEST",
    targeting: 'providerCohorts.panel_local_testing == "SHOW_TEST"',
    template: "spotlight",
    content: {
      template: "multistage",
      screens: [
        {
          id: "SCREEN_1",
          content: {
            screen_style: {
              display: "block",
              padding: "20px 0 0 0",
              width: "560px",
              overflow: "auto",
            },
            logo: {
              height: "40px",
              width: "40",
            },
            title: {
              raw: "Content tiles test",
            },
            subtitle: {
              raw: "Review the content below before continuing.",
              fontSize: "15px",
            },
            tiles_header: { title: "Click to toggle content tiles." },
            tiles: [
              {
                type: "embedded_browser",
                header: {
                  title: "Test title 1",
                  subtitle: "Read more",
                },
                data: {
                  style: {
                    width: "100%",
                    height: "200px",
                  },
                  url: "https://example.com",
                },
              },
              {
                type: "embedded_browser",
                header: {
                  title: "Test Title 2",
                },
                data: {
                  style: {
                    width: "100%",
                    height: "200px",
                  },
                  url: "https://example.com",
                },
              },
              {
                type: "multiselect",
                style: { marginBlock: "18px" },
                data: [
                  {
                    id: "checkbox-test-1",
                    type: "checkbox",
                    defaultValue: true,
                    label: {
                      raw: "Test option 1",
                    },
                    description: {
                      raw: "This is description text explaining text option 1. This pins to taskbar.",
                    },
                    action: {
                      type: "MULTI_ACTION",
                      data: {
                        orderedExecution: true,
                        actions: [
                          {
                            type: "SET_PREF",
                            data: {
                              pref: {
                                name: "test-pref-1",
                                value: true,
                              },
                            },
                          },
                          {
                            type: "PIN_FIREFOX_TO_TASKBAR",
                          },
                        ],
                      },
                    },
                  },
                  {
                    id: "checkbox-test-2",
                    type: "checkbox",
                    defaultValue: false,
                    label: {
                      raw: "Test option 2",
                    },
                    description: {
                      raw: "This is description text explaining text option 2. This sets as default.",
                    },
                    action: {
                      type: "MULTI_ACTION",
                      data: {
                        orderedExecution: true,
                        actions: [
                          {
                            type: "SET_PREF",
                            data: {
                              pref: {
                                name: "test-pref-2",
                                value: true,
                              },
                            },
                          },
                          {
                            type: "SET_DEFAULT_BROWSER",
                          },
                        ],
                      },
                    },
                  },
                ],
              },
            ],
            action_buttons_above_content: true,
            primary_button: {
              label: {
                raw: "Continue",
                marginBlock: "30px 0",
              },
              action: {
                type: "MULTI_ACTION",
                collectSelect: true,
                data: {
                  orderedExecution: true,
                  actions: [
                    {
                      type: "SET_PREF",
                      data: {
                        pref: {
                          name: "test-pref-3",
                          value: true,
                        },
                      },
                    },
                  ],
                },
                dismiss: true,
              },
            },
          },
        },
      ],
    },
    provider: "panel_local_testing",
  },
  {
    id: "SET_DEFAULT_SPOTLIGHT_TEST",
    template: "spotlight",
    content: {
      template: "multistage",
      id: "SET_DEFAULT_SPOTLIGHT",
      screens: [
        {
          id: "PROMPT_CLONE",
          content: {
            width: "377px",
            tiles: {
              type: "multiselect",
              style: {
                flexDirection: "column",
                alignItems: "flex-start",
              },
              data: [
                {
                  id: "checkbox-dont-show-again",
                  type: "checkbox",
                  defaultValue: false,
                  style: {
                    alignItems: "center",
                  },
                  label: {
                    raw: "Don't show this message again",
                    fontSize: "13px",
                  },
                  icon: {
                    style: {
                      width: "16px",
                      height: "16px",
                      marginInline: "0 8px",
                    },
                  },
                  action: {
                    type: "SET_PREF",
                    data: {
                      pref: {
                        name: "browser.shell.checkDefaultBrowser",
                        value: false,
                      },
                    },
                  },
                },
              ],
            },
            isSystemPromptStyleSpotlight: true,
            title_logo: {
              imageURL: "chrome://browser/content/assets/focus-logo.svg",
              height: "16px",
              width: "16px",
            },
            title: {
              fontSize: "13px",
              raw: "Make Nightly your default browser?",
            },
            subtitle: {
              fontSize: "13px",
              raw: "Keep Nightly at your fingertips — make it your default browser and keep it in your Dock.",
            },
            additional_button: {
              label: {
                raw: "Not now",
              },
              style: "secondary",
              action: {
                type: "MULTI_ACTION",
                collectSelect: true,
                data: {
                  actions: [],
                },
                dismiss: true,
              },
            },
            primary_button: {
              label: {
                raw: "Set as primary browser",
              },
              action: {
                type: "MULTI_ACTION",
                collectSelect: true,
                data: {
                  actions: [
                    {
                      type: "PIN_AND_DEFAULT",
                    },
                  ],
                },
                dismiss: true,
              },
            },
          },
        },
      ],
    },
  },
  {
    id: "FEATURE_CALLOUT_ADDON_TEST",
    template: "feature_callout",
    description: "Test ASRouter support for installing or updating addons.",
    content: {
      id: "FEATURE_CALLOUT_ADDON_TEST",
      position: "callout",
      backdrop: "transparent",
      transitions: false,
      disableHistoryUpdates: true,
      metrics: "block",
      screens: [
        {
          id: "FEATURE_CALLOUT_ADDON_TEST",
          anchors: [
            {
              selector: "#unified-extensions-button",
              panel_position: {
                anchor_attachment: "bottomcenter",
                callout_attachment: "topright",
              },
            },
          ],
          content: {
            position: "callout",
            width: "332px",
            logo: {
              imageURL:
                "https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/7dee7c77-1940-4d21-8e56-f1a4dd3c35f4.svg",
              darkModeImageURL:
                "https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/dbbff09d-3c15-4d59-8f8d-cc7eebb9979c.svg",
              height: "182px",
            },
            tiles: {
              type: "addons-picker",
              data: [
                {
                  id: "d634138d-c276-4fc8-924b-40a0ea21d284",
                  name: "1Password",
                  icon: "https://addons.mozilla.org/user-media/addon_icons/950/950528-64.png?modified=bd35add9",
                  type: "extension",
                  description:
                    "Easily sign in to sites, generate passwords, and store secure information, including logins, credit cards, notes, and more.",
                  source_id: "ADD_EXTENSION_BUTTON",
                  action: {
                    type: "INSTALL_ADDON_FROM_URL",
                    advance_screens: { direction: 1 },
                    data: {
                      url: "https://addons.mozilla.org/firefox/downloads/file/4321525/1password_x_password_manager-2.26.1.xpi",
                      telemetrySource: "featurecallout-addon",
                    },
                  },
                },
              ],
            },
            title: {
              string_id: "amo-picker-title",
            },
            subtitle: {
              string_id: "amo-picker-subtitle",
            },
            dismiss_button: {
              size: "small",
              action: {
                dismiss: true,
              },
              background: true,
              marginInline: "0 24px",
              marginBlock: "24px 0",
            },
          },
        },
      ],
    },
    targeting: 'providerCohorts.panel_local_testing == "SHOW_TEST"',
    groups: [],
    provider: "panel_local_testing",
  },
  {
    id: "WNP_THANK_YOU",
    template: "update_action",
    content: {
      action: {
        id: "moments-wnp",
        data: {
          url: "https://www.mozilla.org/%LOCALE%/etc/firefox/retention/thank-you-a/",
          expireDelta: TWO_DAYS,
        },
      },
    },
    trigger: { id: "momentsUpdate" },
  },
  {
    id: "PERSONALIZED_CFR_MESSAGE",
    template: "cfr_doorhanger",
    groups: ["cfr"],
    content: {
      layout: "icon_and_message",
      category: "cfrFeatures",
      bucket_id: "PERSONALIZED_CFR_MESSAGE",
      notification_text: "Personalized CFR Recommendation",
      heading_text: { string_id: "cfr-doorhanger-bookmark-fxa-header" },
      info_icon: {
        label: {
          attributes: {
            tooltiptext: { string_id: "cfr-doorhanger-fxa-close-btn-tooltip" },
          },
        },
        sumo_path: "https://example.com",
      },
      text: { string_id: "cfr-doorhanger-bookmark-fxa-body-2" },
      icon: "chrome://branding/content/icon64.png",
      icon_class: "cfr-doorhanger-large-icon",
      persistent_doorhanger: true,
      buttons: {
        primary: {
          label: { string_id: "cfr-doorhanger-milestone-ok-button" },
          action: {
            type: "OPEN_URL",
            data: {
              args: "https://send.firefox.com/login/?utm_source=activity-stream&entrypoint=activity-stream-cfr-pdf",
              where: "tabshifted",
            },
          },
        },
        secondary: [
          {
            label: { string_id: "cfr-doorhanger-extension-cancel-button" },
            action: { type: "CANCEL" },
          },
          {
            label: {
              string_id: "cfr-doorhanger-extension-never-show-recommendation",
            },
          },
          {
            label: {
              string_id: "cfr-doorhanger-extension-manage-settings-button",
            },
            action: {
              type: "OPEN_PREFERENCES_PAGE",
              data: { category: "general-cfrfeatures" },
            },
          },
        ],
      },
    },
    targeting: "scores.PERSONALIZED_CFR_MESSAGE.score > scoreThreshold",
    trigger: {
      id: "openURL",
      patterns: ["*://*/*.pdf"],
    },
  },
  {
    id: "TEST_BMB_BUTTON",
    groups: [],
    template: "bookmarks_bar_button",
    content: {
      label: {
        raw: "Getting Started",
        tooltiptext: "Getting started with Firefox",
      },
      logo: {
        imageURL: "chrome://browser/content/assets/focus-logo.svg",
      },
      action: {
        type: "MULTI_ACTION",
        navigate: true,
        data: {
          actions: [
            {
              type: "SET_PREF",
              data: {
                pref: {
                  name: "testpref.test.test",
                  value: true,
                },
              },
            },
            {
              type: "OPEN_URL",
              data: {
                args: "https://www.mozilla.org",
                where: "tab",
              },
            },
          ],
        },
      },
    },
    frequency: { lifetime: 100 },
    trigger: { id: "defaultBrowserCheck" },
    targeting: "true",
  },
  {
    id: "MULTISTAGE_SPOTLIGHT_MESSAGE",
    groups: ["panel-test-provider"],
    template: "spotlight",
    content: {
      id: "MULTISTAGE_SPOTLIGHT_MESSAGE",
      template: "multistage",
      backdrop: "transparent",
      transitions: true,
      screens: [
        {
          id: "AW_PIN_FIREFOX",
          content: {
            has_noodles: true,
            title: {
              string_id: "onboarding-easy-setup-security-and-privacy-title",
            },
            logo: {
              imageURL: "chrome://browser/content/callout-tab-pickup.svg",
              darkModeImageURL:
                "chrome://browser/content/callout-tab-pickup-dark.svg",
              reducedMotionImageURL:
                "chrome://browser/content/callout-tab-pickup.svg",
              darkModeReducedMotionImageURL:
                "chrome://browser/content/callout-tab-pickup-dark.svg",
              alt: "sample alt text",
            },
            hero_text: {
              string_id: "fx100-thank-you-hero-text",
            },
            primary_button: {
              label: {
                string_id: isMSIX
                  ? "mr2022-onboarding-pin-primary-button-label-msix"
                  : "mr2022-onboarding-pin-primary-button-label",
              },
              action: {
                type: "MULTI_ACTION",
                navigate: true,
                data: {
                  actions: [
                    {
                      type: "PIN_FIREFOX_TO_TASKBAR",
                    },
                    {
                      type: "PIN_FIREFOX_TO_START_MENU",
                    },
                  ],
                },
              },
            },
            secondary_button: {
              label: {
                string_id: "onboarding-not-now-button-label",
              },
              action: {
                navigate: true,
              },
            },
            dismiss_button: {
              action: {
                dismiss: true,
              },
            },
          },
        },
        {
          id: "AW_SET_DEFAULT",
          content: {
            has_noodles: true,
            logo: {
              imageURL: "chrome://browser/content/logos/vpn-promo-logo.svg",
              height: "100px",
            },
            title: {
              fontSize: "36px",
              fontWeight: 276,
              string_id: "mr2022-onboarding-set-default-title",
            },
            subtitle: {
              string_id: "mr2022-onboarding-set-default-subtitle",
            },
            primary_button: {
              label: {
                string_id: "mr2022-onboarding-set-default-primary-button-label",
              },
              action: {
                navigate: true,
                type: "SET_DEFAULT_BROWSER",
              },
            },
            secondary_button: {
              label: {
                string_id: "onboarding-not-now-button-label",
              },
              action: {
                navigate: true,
              },
            },
          },
        },
        {
          id: "BACKGROUND_IMAGE",
          content: {
            background: "#000",
            text_color: "light",
            progress_bar: true,
            logo: {
              imageURL:
                "chrome://activity-stream/content/data/content/assets/fox-doodle-waving-static.png",
            },
            title: "A dialog with a background",
            subtitle:
              "The text color is configurable and a progress bar style step indicator is used",
            primary_button: {
              label: "Continue",
              action: {
                navigate: true,
              },
            },
            secondary_button: {
              label: {
                string_id: "onboarding-not-now-button-label",
              },
              action: {
                navigate: true,
              },
            },
          },
        },
        {
          id: "BACKGROUND_COLOR",
          content: {
            background: "white",
            progress_bar: true,
            logo: {
              height: "200px",
              imageURL:
                "chrome://activity-stream/content/data/content/assets/fox-doodle-tail.png",
            },
            title: {
              fontSize: "36px",
              fontWeight: 276,
              raw: "Peace of mind.",
            },
            title_style: "fancy shine",
            text_color: "dark",
            subtitle: "Using progress bar style step indicator",
            primary_button: {
              label: "Continue",
              action: {
                navigate: true,
              },
            },
            secondary_button: {
              label: {
                string_id: "onboarding-not-now-button-label",
              },
              action: {
                navigate: true,
              },
            },
          },
        },
      ],
    },
    frequency: { lifetime: 3 },
    trigger: { id: "defaultBrowserCheck" },
  },
  {
    id: "PB_FOCUS_PROMO",
    groups: ["panel-test-provider"],
    template: "spotlight",
    content: {
      template: "multistage",
      backdrop: "transparent",
      screens: [
        {
          id: "PBM_FIREFOX_FOCUS",
          order: 0,
          content: {
            logo: {
              imageURL: "chrome://browser/content/assets/focus-logo.svg",
              height: "48px",
            },
            title: {
              string_id: "spotlight-focus-promo-title",
            },
            subtitle: {
              string_id: "spotlight-focus-promo-subtitle",
            },
            dismiss_button: {
              action: {
                dismiss: true,
              },
            },
            ios: {
              action: {
                data: {
                  args: "https://app.adjust.com/167k4ih?campaign=firefox-desktop&adgroup=pb&creative=focus-omc172&redirect=https%3A%2F%2Fapps.apple.com%2Fus%2Fapp%2Ffirefox-focus-privacy-browser%2Fid1055677337",
                  where: "tabshifted",
                },
                type: "OPEN_URL",
                navigate: true,
              },
            },
            android: {
              action: {
                data: {
                  args: "https://app.adjust.com/167k4ih?campaign=firefox-desktop&adgroup=pb&creative=focus-omc172&redirect=https%3A%2F%2Fplay.google.com%2Fstore%2Fapps%2Fdetails%3Fid%3Dorg.mozilla.focus",
                  where: "tabshifted",
                },
                type: "OPEN_URL",
                navigate: true,
              },
            },
            email_link: {
              action: {
                data: {
                  args: "https://mozilla.org",
                  where: "tabshifted",
                },
                type: "OPEN_URL",
                navigate: true,
              },
            },
            tiles: {
              type: "mobile_downloads",
              data: {
                QR_code: {
                  image_url:
                    "chrome://browser/content/assets/focus-qr-code.svg",
                  alt_text: {
                    string_id: "spotlight-focus-promo-qr-code",
                  },
                },
                email: {
                  link_text: "Email yourself a link",
                },
                marketplace_buttons: ["ios", "android"],
              },
            },
          },
        },
      ],
    },
    trigger: { id: "defaultBrowserCheck" },
  },
  {
    id: "PB_PIN_PROMO",
    template: "pb_newtab",
    groups: ["pbNewtab"],
    content: {
      promoEnabled: true,
      promoType: "PIN",
      promoHeader: "Private browsing freedom in one click",
      promoImageLarge:
        "chrome://browser/content/assets/private-promo-asset.svg",
      promoLinkText: "Pin To Taskbar",
      promoLinkType: "button",
      promoSectionStyle: "below-search",
      promoTitle:
        "No saved cookies or history, right from your desktop. Browse like no one’s watching.",
      promoTitleEnabled: true,
      promoButton: {
        action: {
          type: "MULTI_ACTION",
          data: {
            actions: [
              {
                type: "SET_PREF",
                data: {
                  pref: {
                    name: "browser.privateWindowSeparation.enabled",
                    value: true,
                  },
                },
              },
              {
                type: "PIN_FIREFOX_TO_TASKBAR",
              },
              {
                type: "PIN_FIREFOX_TO_START_MENU",
              },
              {
                type: "BLOCK_MESSAGE",
                data: {
                  id: "PB_PIN_PROMO",
                },
              },
              {
                type: "OPEN_ABOUT_PAGE",
                data: { args: "privatebrowsing", where: "current" },
              },
            ],
          },
        },
      },
    },
    priority: 3,
    frequency: {
      custom: [
        {
          cap: 3,
          period: 604800000, // Max 3 per week
        },
      ],
      lifetime: 12,
    },
    targeting:
      "region != 'CN' && !hasActiveEnterprisePolicies && doesAppNeedPin",
  },
  {
    id: "TEST_TOAST_NOTIFICATION1",
    template: "toast_notification",
    content: {
      title: {
        string_id: "cfr-doorhanger-bookmark-fxa-header",
      },
      body: "Body",
      image_url:
        "chrome://browser/content/asrouter/assets/fox-with-profiles.svg",
      launch_url: "https://mozilla.org",
      requireInteraction: true,
      actions: [
        {
          action: "dismiss",
          title: "Dismiss",
          windowsSystemActivationType: true,
        },
        {
          action: "snooze",
          title: "Snooze",
          windowsSystemActivationType: true,
        },
        { action: "callback", title: "Callback" },
      ],
      tag: "test_toast_notification",
    },
    groups: ["panel-test-provider"],
    targeting: "!hasActiveEnterprisePolicies",
    trigger: { id: "backgroundTask" },
    frequency: { lifetime: 3 },
  },
  {
    id: "TEST_TOAST_NOTIFICATION2",
    template: "toast_notification",
    content: {
      title: "Launch action on toast click and on action button click",
      body: "Body",
      image_url:
        "chrome://browser/content/asrouter/assets/fox-with-profiles.svg",
      launch_action: {
        type: "OPEN_URL",
        data: { args: "https://mozilla.org", where: "window" },
      },
      requireInteraction: true,
      actions: [
        {
          action: "dismiss",
          title: "Dismiss",
          windowsSystemActivationType: true,
        },
        {
          action: "snooze",
          title: "Snooze",
          windowsSystemActivationType: true,
        },
        {
          action: "private",
          title: "Private Window",
          launch_action: { type: "OPEN_PRIVATE_BROWSER_WINDOW" },
        },
      ],
      tag: "test_toast_notification",
    },
    groups: ["panel-test-provider"],
    targeting: "!hasActiveEnterprisePolicies",
    trigger: { id: "backgroundTask" },
    frequency: { lifetime: 3 },
  },
  {
    id: "TEST_TOAST_NOTIFICATION_DYNAMIC_IMAGE",
    template: "toast_notification",
    content: {
      title: "Dynamic image toast",
      body: "This notification shows different images based on dark mode and reduced motion settings.",
      image_url:
        "chrome://browser/content/asrouter/assets/tabgroups/hort-animated-light.svg",
      dark_mode_image_url:
        "chrome://browser/content/asrouter/assets/tabgroups/hort-animated-dark.svg",
      reduced_motion_image_url:
        "chrome://browser/content/asrouter/assets/tabgroups/hort-static-light.svg",
      dark_mode_reduced_motion_image_url:
        "chrome://browser/content/asrouter/assets/tabgroups/hort-static-dark.svg",
      tag: "test_toast_notification_dynamic_image",
    },
    groups: ["panel-test-provider"],
    targeting: "!hasActiveEnterprisePolicies",
    trigger: { id: "backgroundTask" },
    frequency: { lifetime: 3 },
  },
  {
    id: "TEST_TOAST_NOTIFICATION_DYNAMIC_IMAGE_GIF",
    template: "toast_notification",
    content: {
      title: "Dynamic image toast (animated GIF)",
      body: "Each variant (dark mode, reduced motion) shows a different animated GIF.",
      image_url:
        "chrome://activity-stream/content/data/content/assets/fox-doodle-waving.gif",
      dark_mode_image_url:
        "https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/de2349f0-3853-454e-a7c6-100679604cab.gif",
      reduced_motion_image_url:
        "https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/8dc86aae-17d1-4803-b7cc-5369210a9cf8.gif",
      dark_mode_reduced_motion_image_url:
        "https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/94615589-5b64-4028-99d0-5b34f20331c4.gif",
      tag: "test_toast_notification_dynamic_image_gif",
    },
    groups: ["panel-test-provider"],
    targeting: "!hasActiveEnterprisePolicies",
    trigger: { id: "backgroundTask" },
    frequency: { lifetime: 3 },
  },
  {
    id: "IMPORT_SETTINGS_EMBEDDED",
    groups: ["panel-test-provider"],
    template: "spotlight",
    content: {
      template: "multistage",
      backdrop: "transparent",
      screens: [
        {
          id: "IMPORT_SETTINGS_EMBEDDED",
          content: {
            logo: {},
            tiles: { type: "migration-wizard" },
            progress_bar: true,
            migrate_start: {
              action: {},
            },
            migrate_close: {
              action: {
                navigate: true,
              },
            },
            secondary_button: {
              label: {
                string_id: "mr2022-onboarding-secondary-skip-button-label",
              },
              action: {
                navigate: true,
              },
              has_arrow_icon: true,
            },
          },
        },
      ],
    },
  },
  {
    id: "TEST_FEATURE_TOUR",
    template: "feature_callout",
    groups: [],
    content: {
      id: "TEST_FEATURE_TOUR",
      template: "multistage",
      backdrop: "transparent",
      transitions: false,
      disableHistoryUpdates: true,
      screens: [
        {
          id: "FEATURE_CALLOUT_1",
          anchors: [
            {
              selector: "#PanelUI-menu-button",
              panel_position: {
                anchor_attachment: "bottomcenter",
                callout_attachment: "topright",
              },
            },
            {
              selector: "#PanelUI-menu-button",
              panel_position: {
                anchor_attachment: "leftcenter",
                callout_attachment: "rightcenter",
              },
            },
          ],
          content: {
            position: "callout",
            title: { raw: "Panel Feature Callout" },
            subtitle: { raw: "Hello!" },
            secondary_button: {
              label: { raw: "Cancel" },
              action: { advance_screens: { direction: 1 } },
            },
            submenu_button: {
              submenu: [
                {
                  type: "action",
                  label: { raw: "Item 1" },
                  action: { advance_screens: { direction: 1 } },
                  id: "item1",
                },
                {
                  type: "action",
                  label: { raw: "Item 2" },
                  action: { advance_screens: { direction: 1 } },
                  id: "item2",
                },
                {
                  type: "menu",
                  label: { raw: "Menu 1" },
                  submenu: [
                    {
                      type: "action",
                      label: { raw: "Item 3" },
                      action: {
                        advance_screens: { direction: 1 },
                      },
                      id: "item3",
                    },
                    {
                      type: "action",
                      label: { raw: "Item 4" },
                      action: {
                        advance_screens: { direction: 1 },
                      },
                      id: "item4",
                    },
                  ],
                  id: "menu1",
                },
              ],
              attached_to: "secondary_button",
            },
            primary_button: {
              label: { raw: "Advance" },
              action: { advance_screens: { direction: 1 } },
            },
            dismiss_button: { action: { dismiss: true } },
          },
        },
        {
          id: "FEATURE_CALLOUT_2",
          anchors: [
            {
              selector: "#PanelUI-menu-button",
              panel_position: {
                anchor_attachment: "bottomcenter",
                callout_attachment: "topright",
              },
            },
          ],
          content: {
            position: "callout",
            title: { raw: "Panel Feature Callout 2" },
            subtitle: { raw: "Hellossss!" },
            secondary_button: {
              label: { raw: "Advance" },
              action: { advance_screens: { direction: 1 } },
            },
            primary_button: {
              label: { raw: "Go back" },
              style: "secondary",
              action: { advance_screens: { direction: -1 } },
            },
            submenu_button: {
              submenu: [
                {
                  type: "action",
                  label: { raw: "Item 1" },
                  action: { advance_screens: { direction: 1 } },
                  id: "item1",
                },
                {
                  type: "action",
                  label: { raw: "Item 2" },
                  action: { advance_screens: { direction: 1 } },
                  id: "item2",
                },
                {
                  type: "menu",
                  label: { raw: "Menu 1" },
                  submenu: [
                    {
                      type: "action",
                      label: { raw: "Item 3" },
                      action: {
                        advance_screens: { direction: 1 },
                      },
                      id: "item3",
                    },
                    {
                      type: "action",
                      label: { raw: "Item 4" },
                      action: {
                        advance_screens: { direction: 1 },
                      },
                      id: "item4",
                    },
                  ],
                  id: "menu1",
                },
              ],
              attached_to: "secondary_button",
            },
            dismiss_button: { action: { dismiss: true } },
          },
        },
        {
          id: "FEATURE_CALLOUT_3",
          anchors: [
            {
              selector: "#stop-reload-button",
              panel_position: {
                anchor_attachment: "bottomcenter",
                callout_attachment: "topleft",
              },
            },
          ],
          content: {
            position: "callout",
            title: { raw: "Panel Feature Callout" },
            subtitle: { raw: "Screen 2!" },
            secondary_button: {
              label: { raw: "Finish" },
              style: "primary",
              action: {
                type: "MULTI_ACTION",
                collectSelect: true,
                dismiss: true,
                data: { actions: [] },
              },
              disabled: "hasActiveMultiSelect",
            },
            primary_button: {
              label: { raw: "Go back" },
              style: "secondary",
              action: { advance_screens: { direction: -1 } },
            },
            tiles: {
              type: "multiselect",
              style: {
                flexDirection: "column",
                alignItems: "flex-start",
              },
              data: [
                {
                  id: "radio-choice-1",
                  type: "radio",
                  group: "radios",
                  icon: {
                    style: {
                      width: "14px",
                      height: "14px",
                      marginInline: "0 0.5em",
                    },
                  },
                  label: { raw: "Choice 1" },
                },
                {
                  id: "radio-choice-2",
                  type: "radio",
                  group: "radios",
                  icon: {
                    style: {
                      width: "14px",
                      height: "14px",
                      marginInline: "0 0.5em",
                    },
                  },
                  label: { raw: "Choice 2" },
                },
                {
                  id: "radio-choice-3",
                  type: "radio",
                  group: "radios",
                  icon: {
                    style: {
                      width: "14px",
                      height: "14px",
                      marginInline: "0 0.5em",
                    },
                  },
                  label: { raw: "Choice 3" },
                },
                {
                  id: "radio-choice-4",
                  type: "radio",
                  group: "radios",
                  icon: {
                    style: {
                      width: "14px",
                      height: "14px",
                      marginInline: "0 0.5em",
                    },
                  },
                  label: { raw: "Choice 4" },
                },
                {
                  id: "radio-choice-5",
                  type: "radio",
                  group: "radios",
                  icon: {
                    style: {
                      width: "14px",
                      height: "14px",
                      marginInline: "0 0.5em",
                    },
                  },
                  label: { raw: "Choice 5" },
                },
                {
                  id: "radio-choice-6",
                  type: "radio",
                  group: "radios",
                  icon: {
                    style: {
                      width: "14px",
                      height: "14px",
                      marginInline: "0 0.5em",
                    },
                  },
                  label: { raw: "Choice 6" },
                },
              ],
            },
            dismiss_button: {
              action: { dismiss: true },
            },
          },
        },
      ],
    },
  },
  {
    id: "TEST_MULTI_TILES_SURVEY",
    template: "feature_callout",
    groups: [],
    content: {
      id: "TEST_MULTI_TILES_SURVEY",
      template: "multistage",
      backdrop: "transparent",
      transitions: false,
      disableHistoryUpdates: true,
      screens: [
        {
          id: "SCREEN_1",
          force_hide_steps_indicator: true,
          anchors: [
            {
              selector: "hbox#browser",
              hide_arrow: true,
              absolute_position: {
                bottom: "20px",
                right: "20px",
              },
            },
          ],
          content: {
            position: "callout",
            title: {
              raw: "Test title",
            },
            width: "297px",
            title_logo: {
              imageURL:
                "chrome://browser/content/asrouter/assets/smiling-fox-icon.svg",
            },
            subtitle: {
              raw: "Answer two questions!",
            },
            primary_button: {
              label: "Submit",
              action: {
                dismiss: true,
              },
            },
            dismiss_button: {
              action: {
                dismiss: true,
              },
            },
            tiles_container: {
              style: {
                marginBlock: "6px",
              },
            },
            tiles: [
              {
                type: "multiselect",
                title: {
                  raw: "Question 1",
                },
                data: [
                  {
                    id: "checkbox-1",
                    defaultValue: false,
                    label: {
                      raw: "Checkbox 1",
                    },
                  },
                  {
                    id: "checkbox-2",
                    defaultValue: false,
                    label: {
                      raw: "Checkbox 2",
                    },
                  },
                  {
                    id: "checkbox-3",
                    defaultValue: false,
                    label: {
                      raw: "Checkbox 3",
                    },
                  },
                  {
                    id: "checkbox-4",
                    defaultValue: false,
                    label: {
                      raw: "Checkbox 4",
                    },
                  },
                ],
              },
              {
                type: "multiselect",
                title: {
                  raw: "Question 2",
                },
                data: [
                  {
                    id: "checkbox-1",
                    defaultValue: false,
                    label: {
                      raw: "Checkbox 1",
                    },
                  },
                  {
                    id: "checkbox-2",
                    defaultValue: false,
                    label: {
                      raw: "Checkbox 2",
                    },
                  },
                ],
              },
            ],
          },
        },
      ],
    },
  },
  {
    id: "EXPERIMENT_L10N_TEST",
    template: "feature_callout",
    description:
      "Test ASRouter support for flattening experiment-translated messages into plain English text. See bug 1899439.",
    content: {
      id: "EXPERIMENT_L10N_TEST",
      template: "multistage",
      backdrop: "transparent",
      transitions: false,
      disableHistoryUpdates: true,
      metrics: "block",
      screens: [
        {
          id: "EXPERIMENT_L10N_TEST_1",
          anchors: [
            {
              selector: "#PanelUI-menu-button",
              panel_position: {
                anchor_attachment: "bottomcenter",
                callout_attachment: "topright",
              },
            },
          ],
          content: {
            position: "callout",
            layout: "survey",
            width: "min-content",
            padding: "16",
            title: {
              raw: {
                $l10n: {
                  id: "question-title",
                  text: "Help Firefox improve this page",
                  comment:
                    "The title of a popup asking the user to give feedback by answering a short survey",
                },
              },
              marginInline: "0 42px",
              whiteSpace: "nowrap",
            },
            title_logo: {
              imageURL: "chrome://branding/content/about-logo.png",
              alignment: "top",
            },
            subtitle: {
              raw: {
                $l10n: {
                  id: "relevance-question",
                  text: "How relevant are the contents of this Firefox page to you?",
                  comment: "Survey question about relevance",
                },
              },
            },
            secondary_button: {
              label: {
                raw: {
                  $l10n: {
                    id: "advance-button-label",
                    text: "Next",
                    comment:
                      "Label for the button that submits the user's response to question 1 and advances to question 2",
                  },
                },
              },
              style: "primary",
              action: { navigate: true },
              disabled: "hasActiveMultiSelect",
            },
            dismiss_button: {
              size: "small",
              marginBlock: "12px 0",
              marginInline: "0 12px",
              action: { dismiss: true },
            },
            tiles: {
              type: "multiselect",
              style: { flexDirection: "column", alignItems: "flex-start" },
              data: [
                {
                  id: "radio-no-opinion",
                  type: "radio",
                  group: "radios",
                  defaultValue: true,
                  icon: {
                    style: {
                      width: "14px",
                      height: "14px",
                      marginInline: "0 0.5em",
                    },
                  },
                  label: {
                    raw: {
                      $l10n: {
                        id: "radio-no-opinion-label",
                        text: "No opinion",
                        comment:
                          "Answer choice indicating that the user has no opinion about how relevant the New Tab Page is",
                      },
                    },
                  },
                  action: { navigate: true },
                },
              ],
            },
          },
        },
      ],
    },
  },
  {
    id: "FXA_ACCOUNTS_PXIMENU_ROW_LAYOUT",
    template: "menu_message",
    content: {
      messageType: "fxa_cta",
      layout: "row",
      primaryText: "Bounce between devices",
      secondaryText:
        "Sync and encrypt your bookmarks, passwords, and more on all your devices.",
      primaryActionText: "Sign up",
      primaryAction: {
        type: "FXA_SIGNIN_FLOW",
        data: {
          where: "tab",
          extraParams: {
            utm_source: "firefox-desktop",
            utm_medium: "product",
            utm_campaign: "some-campaign",
            utm_content: "some-content",
          },
          autoClose: false,
        },
      },
      closeAction: {
        type: "BLOCK_MESSAGE",
        data: {
          id: "FXA_ACCOUNTS_APPMENU_PROTECT_BROWSING_DATA",
        },
      },
      imageURL: "chrome://browser/content/asrouter/assets/fox-with-devices.svg",
      imageVerticalBottomOffset: -32,
      imageVerticalTopOffset: -4,
      containerVerticalBottomOffset: 20,
      imageWidth: 100,
    },
    skip_in_tests: "TODO",
    trigger: {
      id: "menuOpened",
    },
    testingTriggerContext: "pxi_menu",
  },
  {
    id: "TEST_NEWTAB_MESSAGE",
    template: "newtab_message",
    content: {
      messageType: "DownloadMobilePromoHighlight",
    },
    trigger: {
      id: "newtabMessageCheck",
    },
    groups: [],
  },
  {
    id: "NEWTAB_PERSONALIZATION_MESSAGE",
    template: "newtab_message",
    content: {
      messageType: "PersonalizedCard",
      position: 1,
      cardTitle: "Personalized Just for You",
      cardMessage:
        "We’re customizing your feed to show content that matters to you, while ensuring your privacy is always respected.",
      ctaText: "Manage your settings",
      linkText: "Learn how we protect and manage data",
    },
    trigger: {
      id: "newtabMessageCheck",
    },
    groups: [],
  },
  {
    id: "TEST_ACTIVATION_WINDOW_ENTER_MESSAGE",
    template: "newtab_message",
    content: {
      messageType: "ActivationWindowMessage",
      heading: "On your terms, from the start",
      message:
        "Every tab you open helps supports an independent internet—powered by people like you. Settle in and make Firefox your own.",
      primaryButton: {
        label: "Begin Customizing",
        action: { type: "SHOW_PERSONALIZE" },
      },
    },
    trigger: {
      id: "newtabMessageCheck",
    },
    groups: [],
  },
  {
    id: "TEST_ACTIVATION_WINDOW_EXIT_MESSAGE",
    template: "newtab_message",
    content: {
      messageType: "ActivationWindowMessage",
      heading:
        "We've updated your New Tab with more content we think you'll like",
      message:
        "To make changes, select the pencil icon in the bottom right corner.",
      primaryButton: {
        label: "Got It",
        action: { dismiss: true },
      },
    },
    trigger: {
      id: "newtabMessageCheck",
    },
    groups: [],
  },
  {
    id: "TEST_ACTIVATION_WINDOW_FULL_BLEED_IMAGE",
    template: "newtab_message",
    content: {
      messageType: "ActivationWindowMessage",
      heading: "Welcome to your New Tab!",
      message:
        "To make changes, select the pencil icon in the bottom right corner.",
      imageSrc: "chrome://browser/content/asrouter/assets/kit-peek.svg",
      imageSrcResponsive:
        "chrome://browser/content/asrouter/assets/kit-peek-bottom.svg",
      primaryButton: {
        label: "Got It",
        action: { dismiss: true },
      },
    },
    trigger: {
      id: "newtabMessageCheck",
    },
    groups: [],
  },
  {
    id: "TEST_ASROUTER_NEWTAB_MESSAGE",
    template: "newtab_message",
    content: {
      messageType: "ASRouterNewTabMessage",
      // eslint-disable-next-line mozilla/no-newtab-refs-outside-newtab
      imageSrc: "chrome://newtab/content/data/content/assets/kit-in-circle.svg",
      heading: "Most browsers track you. Firefox blocks that.",
      body: "Many browsers track you. Firefox blocks a lot of that automatically, without extra work.",
      hideDismissButton: false,
      primaryButton: {
        label: "Label",
        action: {
          type: "OPEN_URL",
          data: { args: "https://www.mozilla.org/" },
        },
      },
      secondaryButton: {
        label: "Label",
        action: {
          dismiss: true,
        },
      },
      position: "ABOVE_TOPSITES",
    },
    frequency: {
      lifetime: 3,
    },
    trigger: {
      id: "newtabMessageCheck",
    },
    targeting: `!isDefaultBrowser`,
    groups: ["cfr"],
  },
  {
    id: "TEST_ASROUTER_NEWTAB_MESSAGE_POLL_DEFAULT",
    template: "newtab_message",
    content: {
      messageType: "ASRouterNewTabMessage",
      // eslint-disable-next-line mozilla/no-newtab-refs-outside-newtab
      imageSrc: "chrome://newtab/content/data/content/assets/kit-in-circle.svg",
      heading: "Make Firefox your own",
      body: "Set Firefox as your default browser and pin it so it's always a click away.",
      hideDismissButton: false,
      // Declarative variants: each entry's `targeting` is re-evaluated against
      // the live ASRouter environment (the "uncached" attributes read live
      // shell state), and the first match's content overlays the base content.
      // A `final` entry stops the re-evaluation poll once reached. Here, each
      // state drops whichever step is already satisfied, so only the still-
      // relevant button(s) show: both when neither is done (base content), only
      // "Pin to taskbar" once we're the default, only "Set as default" once
      // we're pinned, and the completed state once both are true.
      states: [
        {
          // Already the default but not pinned: drop the "Set as default" step
          // and only ask to pin.
          targeting: "isDefaultBrowserUncached && doesAppNeedPinUncached",
          content: {
            primaryButton: null,
          },
        },
        {
          // Already pinned but not the default: drop the "Pin to taskbar" step
          // and only ask to set as default.
          targeting: "!isDefaultBrowserUncached && !doesAppNeedPinUncached",
          content: {
            secondaryButton: null,
          },
        },
        {
          // Default and pinned: everything's done. Show the completed state
          // and stop polling.
          targeting: "isDefaultBrowserUncached && !doesAppNeedPinUncached",
          final: true,
          content: {
            heading: "You're all set",
            body: "Firefox is now your default browser and pinned. Thanks!",
            // No `type`, so this renders in the primary style.
            primaryButton: {
              label: "Go to settings to try it out!",
              action: {
                type: "OPEN_ABOUT_PAGE",
                data: { args: "settings#browserIcon", where: "tab" },
              },
            },
            secondaryButton: null,
          },
        },
      ],
      // The base content (neither step done) shows both steps, rendered non-
      // primary (`type: "default"`) since neither is the single call-to-action.
      // The states above swap in narrower content as each step is satisfied.
      primaryButton: {
        label: "Set as default",
        type: "default",
        action: {
          type: "SET_DEFAULT_BROWSER",
        },
      },
      secondaryButton: {
        label: "Pin to taskbar",
        type: "default",
        action: {
          type: "PIN_FIREFOX_TO_TASKBAR",
        },
      },
      position: "ABOVE_TOPSITES",
    },
    frequency: {
      lifetime: 3,
    },
    trigger: {
      id: "newtabMessageCheck",
    },
    targeting: "true",
    groups: ["cfr"],
  },
  {
    id: "TEST_ASROUTER_MULTISTAGE_MESSAGE",
    template: "newtab_message",
    groups: ["cfr"],
    content: {
      messageType: "ASRouterMultistageMessage",
      id: "TEST_ASROUTER_MULTISTAGE_MESSAGE",
      transitions: false,
      backdrop: "transparent",
      screens: [
        {
          id: "SCREEN_1",
          force_hide_steps_indicator: true,
          content: {
            position: "center",
            screen_style: {
              width: "650px",
              height: "500px",
            },
            split_content_padding_block: "32px",
            title: {
              string_id: "create-backup-screen-1-title",
              letterSpacing: "revert",
              whiteSpace: "preserve-breaks",
              lineHeight: "28px",
              marginBlock: "0",
            },
            subtitle: {
              string_id: "create-backup-screen-1-subtitle",
              fontSize: "0.8125em",
              letterSpacing: "revert",
              marginBlock: "12px 0",
            },
            cta_paragraph: {
              text: {
                string_id: "create-backup-learn-more-link",
                string_name: "learn-more-label",
                fontSize: "0.8125em",
              },
              style: {
                marginBlock: "0",
                lineHeight: "100%",
                letterSpacing: "revert",
              },
              action: {
                type: "OPEN_URL",
                data: {
                  args: "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/firefox-backup?utm_medium=firefox-desktop&utm_source=spotlight&utm_campaign=fx-backup-onboarding&utm_content=backup-turn-on-scheduled-learn-more-link&utm_term=fx-backup-onboarding-spotlight-1",
                  where: "tabshifted",
                },
              },
            },
            tiles: {
              type: "single-select",
              autoTrigger: false,
              selected: "sync",
              action: {
                picker: "<event>",
              },
              data: [
                {
                  inert: true,
                  type: "backup",
                  icon: {
                    background:
                      "center / contain no-repeat url('chrome://browser/content/asrouter/assets/fox-with-box-on-cloud.svg')",
                    width: "133.9601px",
                    height: "90.1186px",
                    marginBlockStart: "8px",
                    borderRadius: "5px",
                  },
                  id: "sync",
                  flair: {
                    centered: true,
                    text: {
                      string_id: "create-backup-screen-1-flair",
                      fontSize: "0.625em",
                      fontWeight: "600",
                      lineHeight: "normal",
                    },
                  },
                  label: {
                    string_id: "create-backup-screen-1-sync-label",
                    fontSize: 17,
                    fontWeight: 600,
                  },
                  body: {
                    string_id: "create-backup-screen-1-sync-body",
                    fontSize: "0.625em",
                    fontWeight: "400",
                    marginBlock: "-6px 16px",
                    color: "var(--text-color-deemphasized)",
                  },
                  tilebutton: {
                    label: {
                      string_id: "create-backup-select-tile-button-label",
                      minHeight: "24px",
                      minWidth: "revert",
                      lineHeight: "100%",
                      paddingBlock: "4px",
                      paddingInline: "16px",
                      marginBlock: "0 16px",
                    },
                    style: "primary",
                    action: {
                      type: "FXA_SIGNIN_FLOW",
                      dismiss: "actionResult",
                      needsAwait: true,
                      data: {
                        autoClose: false,
                        entrypoint: "spotlight-create-backup",
                        extraParams: {
                          service: "sync",
                          entrypoint_experiment: "fx-backup-onboarding",
                          entrypoint_variation: "1",
                          utm_medium: "firefox-desktop",
                          utm_source: "spotlight",
                          utm_campaign: "fx-backup-onboarding",
                          utm_term: "fx-backup-onboarding-spotlight-1",
                        },
                      },
                    },
                  },
                },
                {
                  inert: true,
                  type: "backup",
                  icon: {
                    background:
                      "center / contain no-repeat url('chrome://browser/content/asrouter/assets/fox-with-devices.svg')",
                    width: "114.475px",
                    height: "90.1186px",
                    marginBlockStart: "8px",
                    borderRadius: "5px",
                  },
                  id: "backup",
                  flair: {
                    centered: true,
                    spacer: true,
                  },
                  label: {
                    string_id: "create-backup-screen-1-backup-label",
                    fontSize: 17,
                    fontWeight: 600,
                  },
                  body: {
                    string_id: "create-backup-screen-1-backup-body",
                    fontSize: "0.625em",
                    fontWeight: "400",
                    marginBlock: "-6px 16px",
                    color: "var(--text-color-deemphasized)",
                  },
                  tilebutton: {
                    label: {
                      string_id: "create-backup-select-tile-button-label",
                      minHeight: "24px",
                      minWidth: "revert",
                      lineHeight: "100%",
                      paddingBlock: "4px",
                      paddingInline: "16px",
                      marginBlock: "0 16px",
                    },
                    style: "secondary",
                    action: {
                      navigate: true,
                    },
                  },
                },
              ],
            },
          },
        },
        {
          id: "SCREEN_2",
          force_hide_steps_indicator: true,
          content: {
            position: "center",
            screen_style: {
              width: "650px",
              height: "560px",
            },
            split_content_padding_block: "32px",
            title: {
              string_id: "create-backup-screen-2-title",
              letterSpacing: "revert",
              lineHeight: "28px",
              marginBlock: "0",
            },
            subtitle: {
              string_id: "create-backup-screen-2-subtitle",
              fontSize: "0.8125em",
              letterSpacing: "revert",
              marginBlock: "8px 0",
            },
            tiles: {
              type: "single-select",
              selected: "all",
              action: {
                picker: "<event>",
              },
              data: [
                {
                  inert: true,
                  type: "backup",
                  icon: {
                    background:
                      "center / contain no-repeat url('chrome://browser/content/asrouter/assets/shield-checkmark-16.svg')",
                    width: "54px",
                    height: "54px",
                    marginBlockStart: "22px",
                    borderRadius: "5px",
                  },
                  id: "easy",
                  label: {
                    string_id: "create-backup-screen-2-easy-label",
                    fontSize: 17,
                    fontWeight: 600,
                    marginBlock: "3px 10px",
                  },
                  body: {
                    items: [
                      {
                        icon: {
                          background:
                            "center / contain no-repeat url('chrome://browser/content/asrouter/assets/checkmark-16.svg')",
                          height: "18px",
                          width: "18px",
                        },
                        text: {
                          string_id: "create-backup-screen-2-easy-list-1",
                          marginBlock: "4px",
                          fontSize: "13px",
                        },
                      },
                      {
                        icon: {
                          background:
                            "center / contain no-repeat url('chrome://browser/content/asrouter/assets/close-16.svg')",
                          height: "18px",
                          width: "18px",
                        },
                        text: {
                          string_id: "create-backup-screen-2-easy-list-2",
                          marginBlock: "4px",
                          fontSize: "13px",
                        },
                      },
                      {
                        icon: {
                          background:
                            "center / contain no-repeat url('chrome://browser/content/asrouter/assets/close-16.svg')",
                          height: "18px",
                          width: "18px",
                        },
                        text: {
                          string_id: "create-backup-screen-2-easy-list-3",
                          marginBlock: "4px",
                          fontSize: "13px",
                          fontWeight: "600",
                        },
                      },
                    ],
                  },
                  tilebutton: {
                    label: {
                      string_id: "create-backup-select-tile-button-label",
                      minHeight: "24px",
                      minWidth: "revert",
                      lineHeight: "100%",
                      paddingBlock: "4px",
                      paddingInline: "16px",
                      marginBlock: "0 16px",
                    },
                    style: "primary",
                    action: {
                      type: "SET_PREF",
                      navigate: true,
                      data: {
                        pref: {
                          name: "messaging-system-action.backupChooser",
                          value: "easy",
                        },
                      },
                    },
                  },
                },
                {
                  inert: true,
                  type: "backup",
                  icon: {
                    background:
                      "center / contain no-repeat url('chrome://browser/content/asrouter/assets/shield-checkmark-16.svg')",
                    width: "54px",
                    height: "54px",
                    marginBlockStart: "22px",
                    borderRadius: "5px",
                  },
                  id: "all",
                  label: {
                    string_id: "create-backup-screen-2-all-label",
                    fontSize: 17,
                    fontWeight: 600,
                    marginBlock: "3px 10px",
                  },
                  body: {
                    items: [
                      {
                        icon: {
                          background:
                            "center / contain no-repeat url('chrome://browser/content/asrouter/assets/checkmark-16.svg')",
                          height: "18px",
                          width: "18px",
                        },
                        text: {
                          string_id: "create-backup-screen-2-easy-list-1",
                          marginBlock: "4px",
                          fontSize: "13px",
                        },
                      },
                      {
                        icon: {
                          background:
                            "center / contain no-repeat url('chrome://browser/content/asrouter/assets/checkmark-16.svg')",
                          height: "18px",
                          width: "18px",
                        },
                        text: {
                          string_id: "create-backup-screen-2-all-list-2",
                          marginBlock: "4px",
                          fontSize: "13px",
                        },
                      },
                      {
                        icon: {
                          background:
                            "center / contain no-repeat url('chrome://browser/content/asrouter/assets/shield-checkmark-16.svg')",
                          height: "18px",
                          width: "18px",
                        },
                        text: {
                          string_id: "create-backup-screen-2-all-list-3",
                          marginBlock: "4px",
                          fontSize: "13px",
                          fontWeight: 500,
                        },
                      },
                    ],
                  },
                  tilebutton: {
                    label: {
                      string_id: "create-backup-select-tile-button-label",
                      minHeight: "24px",
                      minWidth: "revert",
                      lineHeight: "100%",
                      paddingBlock: "4px",
                      paddingInline: "16px",
                      marginBlock: "0 16px",
                    },
                    marginBlock: "0 16px",
                    style: "primary",
                    action: {
                      type: "SET_PREF",
                      navigate: true,
                      data: {
                        pref: {
                          name: "messaging-system-action.backupChooser",
                          value: "full",
                        },
                      },
                    },
                  },
                },
              ],
            },
            additional_button: {
              style: "secondary",
              label: {
                string_id: "create-backup-back-button-label",
                fontSize: "0.75em",
                minHeight: "24px",
                minWidth: "revert",
                lineHeight: "100%",
                paddingBlock: "4px",
                paddingInline: "12px",
              },
              action: {
                navigate: true,
                goBack: true,
              },
            },
          },
        },
        {
          id: "SCREEN_3A",
          force_hide_steps_indicator: true,
          targeting: "!isEncryptedBackup",
          content: {
            logo: {
              imageURL:
                "chrome://browser/content/asrouter/assets/fox-with-locked-box.svg",
              height: "110px",
            },
            title: {
              raw: "Where do you want your backup saved?",
              paddingBlock: "8px",
              fontSize: "24px",
              fontWeight: 600,
            },
            isEncryptedBackup: false,
            screen_style: {
              width: "650px",
              height: "560px",
            },
            tiles: {
              type: "fx_backup_file_path",
              options: {
                hide_password_input: true,
                file_path_label: "fx-backup-opt-in-filepath-label",
                turn_on_backup_header: "fx-backup-opt-in-header",
                turn_on_backup_confirm_btn_label:
                  "fx-backup-opt-in-confirm-btn-label",
              },
            },
            additional_button: {
              style: "secondary",
              label: {
                raw: "Back",
                fontSize: "0.75em",
                minHeight: "24px",
                minWidth: "revert",
                lineHeight: "100%",
                paddingBlock: "4px",
                paddingInline: "12px",
              },
              action: {
                navigate: true,
                goBack: true,
              },
            },
          },
        },
        {
          id: "SCREEN_3B",
          force_hide_steps_indicator: true,
          targeting: "isEncryptedBackup",
          content: {
            isEncryptedBackup: true,
            logo: {
              imageURL:
                "chrome://browser/content/asrouter/assets/fox-with-locked-box.svg",
              height: "110px",
            },
            title: {
              raw: "Where do you want your backup saved?",
            },
            screen_style: {
              width: "650px",
              height: "560px",
            },
            tiles: {
              type: "fx_backup_file_path",
              options: {
                hide_password_input: true,
                hide_secondary_button: true,
                file_path_label: "fx-backup-opt-in-filepath-label",
                turn_on_backup_header: "fx-backup-opt-in-header",
                turn_on_backup_confirm_btn_label:
                  "fx-backup-opt-in-confirm-btn-label",
              },
            },
            additional_button: {
              style: "secondary",
              flow: "row",
              label: {
                raw: "Back",
                fontSize: "0.75em",
                minHeight: "24px",
                minWidth: "revert",
                lineHeight: "100%",
                paddingBlock: "4px",
                paddingInline: "12px",
              },
              action: {
                navigate: true,
                goBack: true,
              },
            },
          },
        },
        {
          id: "FX_BACKUP_ENCRYPTION",
          force_hide_steps_indicator: true,
          targeting: "isEncryptedBackup",
          content: {
            isEncryptedBackup: true,
            title: {
              raw: "Create a backup file password",
            },
            subtitle: {
              raw: "Required to encrypt your data. Store it in a place you’ll remember.",
              fontSize: "13px",
            },
            screen_style: {
              width: "664px",
              height: "620px",
            },
            logo: {
              imageURL:
                "chrome://browser/content/asrouter/assets/fox-with-locked-box.svg",
              height: "130px",
            },
            tiles: {
              type: "fx_backup_password",
              options: {
                hide_secondary_button: true,
                create_password_label: "fx-backup-opt-in-create-password-label",
                turn_on_backup_confirm_btn_label:
                  "fx-backup-opt-in-confirm-btn-label",
              },
            },
            additional_button: {
              style: "secondary",
              label: {
                raw: "Back",
                fontSize: "0.75em",
                minHeight: "24px",
                minWidth: "revert",
                lineHeight: "100%",
                paddingBlock: "4px",
                paddingInline: "12px",
              },
              action: {
                navigate: true,
                goBack: true,
              },
            },
          },
        },
        {
          id: "BACKUP_CONFIRMATION_SCREEN_EASY",
          force_hide_steps_indicator: true,
          targeting: "!isEncryptedBackup",
          content: {
            screen_style: {
              width: "664px",
              height: "620px",
            },
            logo: {
              imageURL:
                "chrome://browser/content/asrouter/assets/fox-with-checkmark.svg",
              height: "96px",
            },
            title: {
              string_id: "fx-backup-confirmation-screen-title",
            },
            tiles: {
              type: "confirmation-checklist",
              data: {
                inert: true,
                style: { width: "500px" },
                items: [
                  {
                    icon: {
                      background:
                        "center / contain no-repeat url('chrome://browser/content/asrouter/assets/checkmark-16.svg')",
                      height: "18px",
                      width: "18px",
                    },
                    text: {
                      string_id:
                        "fx-backup-confirmation-screen-easy-setup-item-text-1",
                    },
                    subtext: {
                      string_id: "fx-backup-confirmation-screen-item-subtext-1",
                    },
                    link_keys: ["settings"],
                  },
                  {
                    icon: {
                      background:
                        "center / contain no-repeat url('chrome://browser/content/asrouter/assets/checkmark-16.svg')",
                      height: "18px",
                      width: "18px",
                    },
                    text: {
                      string_id:
                        "fx-backup-confirmation-screen-easy-setup-item-text-2",
                    },
                    subtext: {
                      string_id: "fx-backup-confirmation-screen-item-subtext-2",
                    },
                  },
                  {
                    icon: {
                      background:
                        "center / contain no-repeat url('chrome://browser/content/asrouter/assets/subtract-16.svg')",
                      height: "18px",
                      width: "18px",
                    },
                    text: {
                      string_id:
                        "fx-backup-confirmation-screen-easy-setup-item-text-3",
                    },
                    subtext: {
                      string_id:
                        "fx-backup-confirmation-screen-easy-setup-item-subtext-3",
                    },
                    link_keys: ["settings"],
                  },
                ],
              },
            },
            settings: {
              action: {
                type: "OPEN_ABOUT_PAGE",
                data: {
                  args: "preferences#sync-backup",
                  where: "tab",
                },
              },
            },
            additional_button: {
              label: {
                string_id: "fx-backup-confirmation-screen-close-button",
              },
              style: "secondary",
              action: { dismiss: true },
            },
          },
        },
        {
          id: "BACKUP_CONFIRMATION_SCREEN_ENCRYPTED",
          force_hide_steps_indicator: true,
          targeting: "isEncryptedBackup",
          content: {
            isEncryptedBackup: true,
            screen_style: {
              width: "664px",
              height: "620px",
            },
            logo: {
              imageURL:
                "chrome://browser/content/asrouter/assets/fox-with-checkmark.svg",
              height: "96px",
            },
            title: {
              string_id: "fx-backup-confirmation-screen-title",
            },
            tiles: {
              type: "confirmation-checklist",
              data: {
                inert: true,
                style: { width: "500px" },
                items: [
                  {
                    icon: {
                      background:
                        "center / contain no-repeat url('chrome://browser/content/asrouter/assets/checkmark-16.svg')",
                      height: "18px",
                      width: "18px",
                    },
                    text: {
                      string_id:
                        "fx-backup-confirmation-screen-all-data-item-text-1",
                    },
                    subtext: {
                      string_id: "fx-backup-confirmation-screen-item-subtext-1",
                    },
                    link_keys: ["settings"],
                  },
                  {
                    icon: {
                      background:
                        "center / contain no-repeat url('chrome://browser/content/asrouter/assets/checkmark-16.svg')",
                      height: "18px",
                      width: "18px",
                    },
                    text: {
                      string_id:
                        "fx-backup-confirmation-screen-all-data-item-text-2",
                    },
                    subtext: {
                      string_id: "fx-backup-confirmation-screen-item-subtext-2",
                    },
                  },
                  {
                    icon: {
                      background:
                        "center / contain no-repeat url('chrome://browser/content/asrouter/assets/checkmark-16.svg')",
                      height: "18px",
                      width: "18px",
                    },
                    text: {
                      string_id:
                        "fx-backup-confirmation-screen-all-data-item-text-3",
                    },
                  },
                ],
              },
            },
            settings: {
              action: {
                type: "OPEN_ABOUT_PAGE",
                data: {
                  args: "preferences#sync-backup",
                  where: "tab",
                },
              },
            },
            additional_button: {
              label: {
                string_id: "fx-backup-confirmation-screen-close-button",
              },
              style: "secondary",
              action: { dismiss: true },
            },
          },
        },
      ],
    },
    frequency: {
      lifetime: 3,
    },
    trigger: {
      id: "newtabMessageCheck",
    },
  },
  // TEST_NOVA_MULTISTAGE_SPLIT exercises the OMC-owned embedded multi-stage
  // (spotlight) New Tab surface for the Nova design-token audit. Force it on via
  // about:asrouter while Nova is enabled to check token, spacing, and layout
  // rendering. This surface is bundled / multi-stage, so notify HNT of changes.
  {
    id: "TEST_NOVA_MULTISTAGE_SPLIT",
    template: "newtab_message",
    groups: [],
    content: {
      messageType: "ASRouterMultistageMessage",
      id: "TEST_NOVA_MULTISTAGE_SPLIT",
      transitions: false,
      backdrop: "transparent",
      screens: [
        {
          id: "NOVA_SPLIT_SCREEN_1",
          content: {
            position: "split",
            screen_style: { height: "500px" },
            hero_text: {
              title: {
                raw: "Nova multistage, split layout",
                fontSize: "24px",
              },
              subtitle: {
                raw: "First of two screens. Verifies the steps indicator and split hero area under Nova.",
              },
            },
            title: {
              raw: "Screen one",
            },
            subtitle: {
              raw: "Check spacing and contrast of the content column.",
            },
            primary_button: {
              label: { raw: "Next" },
              action: { navigate: true },
            },
          },
        },
        {
          id: "NOVA_SPLIT_SCREEN_2",
          content: {
            position: "split",
            screen_style: { height: "500px" },
            hero_text: {
              title: {
                raw: "Almost done",
                fontSize: "24px",
              },
            },
            title: {
              raw: "Screen two",
            },
            subtitle: {
              raw: "Final screen, confirm the primary and secondary button styling under Nova.",
            },
            primary_button: {
              label: { raw: "Finish" },
              action: { dismiss: true },
            },
            secondary_button: {
              label: { raw: "Not now" },
              // navigate on the last screen falls through to AWFinish (dismiss);
              // this matches the pattern shipping split-screen messages use.
              action: { navigate: true },
            },
          },
        },
      ],
    },
    trigger: {
      id: "newtabMessageCheck",
    },
  },
  {
    id: "UNIVERSAL_INFOBAR_WITH_EMBEDDED_LINKS",
    content: {
      text: [
        "Read the release notes ",
        {
          raw: "here",
          href: "https://www.mozilla.org/en-US/firefox/releases/",
        },
        ". ",
        {
          string_id: "etp-strict-exceptions-infobar-learn-more",
          href: "https://mozilla.org/privacy/firefox/?v=product",
        },
        "!",
      ],
      type: "universal",
      dismissable: false,
      buttons: [
        {
          label: { string_id: "existing-user-tou-accept" },
          action: {
            type: "SET_PREF",
            data: {
              pref: {
                name: "universal-infobar-test-pref",
                value: true,
              },
            },
          },
          primary: true,
          accessKey: "C",
        },
      ],
    },
    trigger: {
      id: "defaultBrowserCheck",
    },
    template: "infobar",
    frequency: {
      lifetime: 100,
    },
  },
  {
    id: "TEST_PROFILE_SPOTLIGHT",
    groups: [],
    targeting: "canCreateSelectableProfiles",
    trigger: {
      id: "defaultBrowserCheck",
    },
    template: "spotlight",
    profileScope: "single",
    frequency: {
      lifetime: 1,
    },
    content: {
      template: "multistage",
      modal: "tab",
      screens: [
        {
          id: "SCREEN_1",
          content: {
            logo: {
              imageURL:
                "https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/a3c640c8-7594-4bb2-bc18-8b4744f3aaf2.gif",
            },
            title: {
              raw: "Firefox profiles Test Message",
            },
            subtitle: {
              raw: "Profiles keep you organized.",
            },
            dismiss_button: {
              action: {
                dismiss: true,
              },
            },
            secondary_button: {
              label: "Close",
              action: {
                dismiss: true,
              },
            },
          },
        },
      ],
      transitions: true,
    },
  },
  {
    id: "TEST_ADDRESS_BAR_MESSAGING_OPEN_NEW_TAB_TRY_NEW_WAY_GOOGLE",
    groups: ["cfr"],
    content: {
      id: "TEST_ADDRESS_BAR_MESSAGING_OPEN_NEW_TAB_TRY_NEW_WAY_GOOGLE",
      screens: [
        {
          id: "TRY_NEW_WAY_SCREEN_GOOGLE",
          anchors: [
            {
              selector: ".urlbar-input-container",
              arrow_width: "26.9",
              no_open_on_anchor: true,
              panel_position: {
                offset_x: 60,
                anchor_attachment: "bottomleft",
                callout_attachment: "topleft",
              },
            },
          ],
          content: {
            title: {
              raw: {
                $l10n: {
                  id: "messaging-new-tab-title-try-new-way",
                  text: "Try a new way to search",
                  comment:
                    "Title for a callout that introduces a new search experience in the address bar",
                },
              },
              paddingInline: "0 40px",
              fontSize: "0.85em",
            },
            width: "300px",
            padding: 16,
            position: "callout",
            subtitle: {
              raw: {
                $l10n: {
                  id: "messaging-new-tab-subtitle-try-new-way-google",
                  text: "Type what you’re looking for in the address bar to get results from your history and from Google.com.",
                  comment:
                    "Subtitle for a callout that introduces a new search experience in the address bar (Google)",
                },
              },
              textAlign: "start",
              fontSize: "0.8em",
              marginBlock: "-4px 0",
            },
            primary_button: {
              label: {
                raw: {
                  $l10n: {
                    id: "messaging-try-it-label",
                    text: "Try it",
                    comment:
                      "Label for the button that dismisses a callout that introduces search suggestions in the address bar",
                  },
                },
                paddingBlock: "4px",
                paddingInline: "16px",
              },
              action: {
                type: "FOCUS_URLBAR",
                dismiss: true,
              },
            },
            secondary_button: {
              label: {
                raw: {
                  $l10n: {
                    id: "messaging-no-thanks-label",
                    text: "No thanks",
                    comment:
                      "Label for the button that dismisses a callout that introduces search suggestions in the address bar",
                  },
                },
              },
              action: {
                dismiss: true,
              },
            },
          },
        },
      ],
    },
    trigger: {
      id: "defaultBrowserCheck",
    },
    template: "feature_callout",
    frequency: {
      custom: [
        {
          cap: 1,
          period: 86400000 * 7,
        },
      ],
      lifetime: 3,
    },
    targeting:
      "source == 'newtab' && !isMajorUpgrade && !activeNotifications && userPrefs.cfrFeatures && previousSessionEnd && !hasActiveEnterprisePolicies",
  },
  {
    id: "TEST_ADDRESS_BAR_MESSAGING_OPEN_NEW_TAB_TIP_SAVE_STEP_GOOGLE",
    groups: ["cfr"],
    content: {
      id: "TEST_ADDRESS_BAR_MESSAGING_OPEN_NEW_TAB_TIP_SAVE_STEP_GOOGLE",
      screens: [
        {
          id: "TIP_SAVE_STEP_SCREEN_GOOGLE",
          anchors: [
            {
              selector: ".urlbar-input-container",
              arrow_width: "26.9",
              no_open_on_anchor: true,
              panel_position: {
                offset_x: 60,
                anchor_attachment: "bottomleft",
                callout_attachment: "topleft",
              },
            },
          ],
          content: {
            title: {
              raw: {
                $l10n: {
                  id: "messaging-new-tab-title-tip-save-step",
                  text: "Search here to save a step",
                  comment:
                    "Title for a callout that introduces search suggestions in the address bar",
                },
              },
              paddingInline: "0 40px",
              fontSize: "0.85em",
            },
            width: "300px",
            padding: 16,
            position: "callout",
            subtitle: {
              raw: {
                $l10n: {
                  id: "messaging-new-tab-subtitle-tip-save-step-google",
                  text: "Firefox never tracks your searches. Type what you’re looking for in the address bar to get results from Google.",
                  comment:
                    "Subtitle for a callout that introduces search suggestions in the address bar (Google)",
                },
              },
              textAlign: "start",
              fontSize: "0.8em",
              marginBlock: "-4px 0",
            },
            primary_button: {
              label: {
                raw: {
                  $l10n: {
                    id: "messaging-try-it-label",
                    text: "Try it",
                    comment:
                      "Label for the button that dismisses a callout that introduces search suggestions in the address bar",
                  },
                },
                paddingBlock: "4px",
                paddingInline: "16px",
              },
              action: {
                type: "FOCUS_URLBAR",
                dismiss: true,
              },
            },
            secondary_button: {
              label: {
                raw: {
                  $l10n: {
                    id: "messaging-no-thanks-label",
                    text: "No thanks",
                    comment:
                      "Label for the button that dismisses a callout that introduces search suggestions in the address bar",
                  },
                },
              },
              action: {
                dismiss: true,
              },
            },
          },
        },
      ],
    },
    trigger: {
      id: "defaultBrowserCheck",
    },
    template: "feature_callout",
    frequency: {
      custom: [
        {
          cap: 1,
          period: 86400000 * 7,
        },
      ],
      lifetime: 3,
    },
    targeting:
      "source == 'newtab' && !isMajorUpgrade && !activeNotifications && userPrefs.cfrFeatures && previousSessionEnd && !hasActiveEnterprisePolicies",
  },
  {
    id: "TEST_CONTENT_ANCHOR",
    template: "feature_callout",
    content: {
      id: "TEST_CONTENT_ANCHOR",
      padding: "16",
      template: "multistage",
      backdrop: "transparent",
      transitions: false,
      disableHistoryUpdates: true,
      screens: [
        {
          id: "TEST_CONTENT_ANCHOR_SCREEN",
          anchors: [
            {
              selector:
                "hbox.deck-selected browser::%document%ai-window::%shadow%context-icon-button::%shadow% .context-icon-button",
              panel_position: {
                anchor_attachment: "bottomcenter",
                callout_attachment: "topright",
              },
            },
          ],
          content: {
            position: "callout",
            width: "400px",
            padding: 16,
            title: {
              raw: "Test Content Anchor",
            },
            subtitle: {
              raw: "This callout is for testing content anchors.",
            },
            additional_button: {
              action: {
                dismiss: true,
              },
              label: {
                string_id: "dismiss-button-label",
                fontWeight: "590",
                fontSize: "11px",
              },
              style: "secondary",
            },
          },
        },
      ],
    },
    frequency: {
      lifetime: 2,
    },
    trigger: {
      id: "smartWindowNewTab",
    },
    targeting: "isAIWindow",
  },
  {
    id: "WORLD_CUP_TASKBAR_TABS_SPOTLIGHT",
    groups: ["panel-test-provider"],
    template: "spotlight",
    content: {
      id: "WORLD_CUP_TASKBAR_TABS_SPOTLIGHT",
      template: "multistage",
      modal: "tab",
      transitions: false,
      screens: [
        {
          id: "SCREEN_1",
          force_hide_steps_indicator: true,
          content: {
            position: "split",
            zap_border: true,
            zap_shadow: true,
            main_content_style: {
              paddingBlockStart: "40px",
            },
            main_content_style_narrow: {
              paddingInline: "40px",
            },
            background:
              "center / contain no-repeat light-dark(url('https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/20260602011346--visuals-worldcup-light--12288900-f5e6-4193-8b66-da63b6cfe806.svg'), url('https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/20260602011258--visuals-worldcup-dark--a975f19d-f186-40bb-8d86-98f450ad5462.svg'))",
            hero_text: {
              title: {
                raw: "Follow the World Cup in Firefox",
                fontSize: "24px",
              },
              subtitle: {
                raw: "One click launches your most used sites in a streamlined window with all of Firefox’s protections.",
                textAlign: "initial",
                marginInline: "40px",
                fontSize: "13px",
              },
            },
            tiles: {
              type: "pinnable_sites",
              title: {
                raw: "Select to add to your taskbar",
                fontSize: "18px",
              },
              pinButtonLabel: { raw: "Add" },
              alwaysShowPinButton: true,
              data: [
                {
                  id: "fifa-plus",
                  name: "FIFA+",
                  description: "plus.fifa.com",
                  iconUrl:
                    "https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/20260602010406--fifa-plus-icon--a3fb720e-4604-43c9-9c7a-aabca6ba5bbb.png",
                  url: "https://plus.fifa.com",
                },
                {
                  id: "fox-sports",
                  name: "Fox Sports",
                  description: "foxsports.com",
                  iconUrl:
                    "https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/20260602010635--fox-sports-icon--f0dd8f03-f5c7-4930-94db-f002eca5c3e5.png",
                  url: "https://www.foxsports.com/",
                },
                {
                  id: "telemundo-deportes",
                  name: "Telemundo Deportes",
                  description: "telemundodeportes.com",
                  iconUrl:
                    "https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/20260602010447--telemundo-deportes-icon--710f3661-b2f2-4e2b-9fa4-c791b9cdb303.png",
                  url: "https://telemundodeportes.com/",
                },
                {
                  id: "r-soccer-reddit",
                  name: "r/soccer Reddit",
                  description: "reddit.com/r/soccer",
                  iconUrl:
                    "https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/20260602010522--reddit-soccer-icon--63d9ec11-d281-42ff-8382-b635cce58864.png",
                  url: "http://reddit.com/r/soccer",
                },
                {
                  id: "fubo-tv",
                  name: "FuboTV",
                  description: "fubo.tv",
                  iconUrl:
                    "https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/20260602010600--fubo-tv-icon--2f47927e-d29c-4000-bb39-9f4a03e1c2e6.png",
                  url: "http://fubo.tv/",
                },
              ],
            },
            primary_button: {
              label: { raw: "Done" },
              disabled: "hasPinnedSite",
              action: { dismiss: true },
            },
            dismiss_button: {
              action: {
                dismiss: true,
              },
            },
          },
        },
      ],
    },
    frequency: {
      lifetime: 100,
    },
    targeting: "true",
  },
  // For manually testing the splitViewUsed trigger. See the "trigger" field.
  {
    id: "SPLIT_VIEW_SMART_WINDOW_CALLOUT",
    template: "feature_callout",
    groups: [],
    provider: "panel_local_testing",
    content: {
      id: "SPLIT_VIEW_SMART_WINDOW_CALLOUT",
      template: "multistage",
      backdrop: "transparent",
      transitions: false,
      screens: [
        {
          id: "SPLIT_VIEW_SMART_WINDOW_CALLOUT",
          anchors: [
            {
              selector: "#split-view-button",
              panel_position: {
                anchor_attachment: "bottomcenter",
                callout_attachment: "topright",
              },
            },
          ],
          content: {
            position: "callout",
            padding: 16,
            width: "330px",
            title: "Working across multiple tabs?",
            subtitle: "Try Smart Window to compare or reason across them.",
            primary_button: {
              label: "Try Smart Window",
              action: {
                type: "MULTI_ACTION",
                dismiss: true,
                data: {
                  actions: [
                    {
                      type: "OPEN_ABOUT_PAGE",
                      data: {
                        args: "newtab",
                        where: "tab",
                      },
                    },
                    { type: "FXA_AIWINDOW_SIGNIN_FLOW" },
                  ],
                },
              },
            },
            dismiss_button: { action: { dismiss: true } },
          },
        },
      ],
    },
    // Example of message targeting on the trigger's splitViewCreateCount
    // context, to exclude users who are creating a Split View for the first
    // time.
    targeting: "splitViewCreateCount > 1",
    trigger: {
      id: "splitViewUsed",
    },
    frequency: {
      lifetime: 2,
    },
  },
  {
    id: "SIDEBAR_CHATBOT_PROMO_TEST",
    template: "sidebar_chatbot_promo",
    content: {
      type: "default",
      heading: "Give your AI the full picture",
      message:
        "In Smart Window, AI reads every page and tab you have open, not just this sidebar. Skip the copy-paste and just ask.",
      primary_button: {
        label: "Try Smart Window",
        action: {
          type: "FXA_AIWINDOW_SIGNIN_FLOW",
        },
      },
      additional_button: {
        label: "Dismiss",
        action: {
          type: "CANCEL",
        },
      },
    },
    trigger: {
      id: "sidebarToolOpened",
    },
    frequency: {
      lifetime: 3,
    },
  },
];

export const PanelTestProvider = {
  getMessages() {
    return Promise.resolve(
      MESSAGES().map(message => ({
        ...message,
        targeting:
          typeof message.targeting === "string" &&
          message.targeting?.includes("isAIWindow")
            ? `isAIWindow && providerCohorts.panel_local_testing == "SHOW_TEST"`
            : `providerCohorts.panel_local_testing == "SHOW_TEST"`,
      }))
    );
  },
};
