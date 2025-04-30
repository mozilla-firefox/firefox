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
    id: "CLOSE_TAB_GROUP_TEST_CALLOUT",
    template: "feature_callout",
    groups: ["cfr"],
    content: {
      id: "CLOSE_TAB_GROUP_TEST_CALLOUT",
      template: "multistage",
      backdrop: "transparent",
      transitions: false,
      screens: [
        {
          id: "CLOSE_TAB_GROUP_TEST_CALLOUT",
          anchors: [
            {
              selector: "#alltabs-button",
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
              raw: "If you close a tab group, you can reopen it here anytime.",
            },
            primary_button: {
              label: {
                raw: "Got it",
              },
              action: {
                dismiss: true,
              },
            },
          },
        },
      ],
    },
    targeting: "tabGroupsClosedCount == 1",
    trigger: {
      id: "tabGroupClosed",
    },
    frequency: {
      lifetime: 1,
    },
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
    id: "TAB_GROUP_TEST_CALLOUT_HORIZONTAL",
    template: "feature_callout",
    groups: ["cfr"],
    content: {
      id: "TAB_GROUP_TEST_CALLOUT_HORIZONTAL",
      template: "multistage",
      backdrop: "transparent",
      transitions: false,
      screens: [
        {
          id: "TAB_GROUP_TEST_CALLOUT_HORIZONTAL",
          anchors: [
            {
              selector: ".tab-content[selected]",
              panel_position: {
                anchor_attachment: "bottomcenter",
                callout_attachment: "topright",
              },
            },
          ],
          content: {
            position: "callout",
            width: "333px",
            padding: 16,
            logo: {
              imageURL:
                "chrome://browser/content/asrouter/assets/tabgroups/hort-animated-light.svg",
              darkModeImageURL:
                "chrome://browser/content/asrouter/assets/tabgroups/hort-animated-dark.svg",
              reducedMotionImageURL:
                "chrome://browser/content/asrouter/assets/tabgroups/hort-static-light.svg",
              darkModeReducedMotionImageURL:
                "chrome://browser/content/asrouter/assets/tabgroups/hort-static-dark.svg",
              height: "172px",
              width: "300px",
            },
            title: {
              raw: "Try tab groups for less clutter, more focus",
            },
            subtitle: {
              raw: "Organize your tabs by dragging one tab on top of another to create your first tab group.",
            },
            primary_button: {
              label: {
                raw: "Got it",
              },
              action: {
                dismiss: true,
              },
            },
            dismiss_button: {
              action: {
                dismiss: true,
              },
              size: "small",
              marginInline: "0 20px",
              marginBlock: "20px 0",
            },
          },
        },
      ],
    },
    targeting: "tabsClosedCount >= 2 && currentTabsOpen >= 4",
    trigger: {
      id: "nthTabClosed",
    },
    frequency: {
      lifetime: 1,
    },
  },
  {
    id: "TAB_GROUP_TEST_CALLOUT_VERTICAL",
    template: "feature_callout",
    groups: ["cfr"],
    content: {
      id: "TAB_GROUP_TEST_CALLOUT_VERTICAL",
      template: "multistage",
      backdrop: "transparent",
      transitions: false,
      screens: [
        {
          id: "TAB_GROUP_TEST_CALLOUT_VERTICAL",
          anchors: [
            {
              selector: ".tab-content[selected]",
              panel_position: {
                anchor_attachment: "rightcenter",
                callout_attachment: "topleft",
              },
            },
          ],
          content: {
            position: "callout",
            width: "333px",
            padding: 16,
            logo: {
              imageURL:
                "chrome://browser/content/asrouter/assets/tabgroups/vert-animated-light.svg",
              darkModeImageURL:
                "chrome://browser/content/asrouter/assets/tabgroups/vert-animated-dark.svg",
              reducedMotionImageURL:
                "chrome://browser/content/asrouter/assets/tabgroups/vert-static-light.svg",
              darkModeReducedMotionImageURL:
                "chrome://browser/content/asrouter/assets/tabgroups/vert-static-dark.svg",
              height: "172px",
              width: "300px",
            },
            title: {
              raw: "Try tab groups for less clutter, more focus",
            },
            subtitle: {
              raw: "Organize your tabs by dragging one tab on top of another to create your first tab group.",
            },
            primary_button: {
              label: {
                raw: "Got it",
              },
              action: {
                dismiss: true,
              },
            },
            dismiss_button: {
              action: {
                dismiss: true,
              },
              size: "small",
              marginInline: "0 20px",
              marginBlock: "20px 0",
            },
          },
        },
      ],
    },
    targeting: "tabsClosedCount >= 2 && currentTabsOpen >= 4",
    trigger: {
      id: "nthTabClosed",
    },
    frequency: {
      lifetime: 1,
    },
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
    id: "NEW_PROFILE_SPOTLIGHT",
    groups: [],
    targeting: "canCreateSelectableProfiles && !hasSelectableProfiles",
    trigger: {
      id: "defaultBrowserCheck",
    },
    template: "spotlight",
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
              raw: "Say hello to Firefox profiles",
              paddingBlock: "8px",
            },
            subtitle: {
              raw: "Easily switch between browsing for work and fun. Profiles keep your browsing info, including search history and passwords, totally separate so you can stay organized.",
            },
            dismiss_button: {
              action: {
                dismiss: true,
              },
            },
            primary_button: {
              label: "Create a profile",
              action: {
                navigate: true,
                type: "CREATE_NEW_SELECTABLE_PROFILE",
              },
            },
            secondary_button: {
              label: "Not now",
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
                "https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/a3c640c8-7594-4bb2-bc18-8b4744f3aaf2.gif",
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
              imageURL: "",
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
    id: "PB_NEWTAB_VPN_PROMO",
    template: "pb_newtab",
    content: {
      promoEnabled: true,
      promoType: "VPN",
      infoEnabled: true,
      infoBody: "fluent:about-private-browsing-info-description-private-window",
      infoLinkText: "fluent:about-private-browsing-learn-more-link",
      infoTitleEnabled: false,
      promoLinkType: "button",
      promoLinkText: "fluent:about-private-browsing-prominent-cta",
      promoSectionStyle: "below-search",
      promoHeader: "fluent:about-private-browsing-get-privacy",
      promoTitle: "fluent:about-private-browsing-hide-activity-1",
      promoTitleEnabled: true,
      promoImageLarge: "chrome://browser/content/assets/moz-vpn.svg",
      promoButton: {
        action: {
          type: "OPEN_URL",
          data: {
            args: "https://vpn.mozilla.org/",
          },
        },
      },
    },
    groups: ["panel-test-provider", "pbNewtab"],
    targeting: "region != 'CN' && !hasActiveEnterprisePolicies",
    frequency: { lifetime: 3 },
  },
  {
    id: "PB_PIN_PROMO",
    template: "pb_newtab",
    groups: ["pbNewtab"],
    content: {
      infoBody: "fluent:about-private-browsing-info-description-simplified",
      infoEnabled: true,
      infoIcon: "chrome://global/skin/icons/indicator-private-browsing.svg",
      infoLinkText: "fluent:about-private-browsing-learn-more-link",
      infoTitle: "",
      infoTitleEnabled: false,
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
    weight: 100,
    template: "toast_notification",
    content: {
      title: {
        string_id: "cfr-doorhanger-bookmark-fxa-header",
      },
      body: "Body",
      image_url:
        "https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/a3c640c8-7594-4bb2-bc18-8b4744f3aaf2.gif",
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
    trigger: { id: "backgroundTaskMessage" },
    frequency: { lifetime: 3 },
  },
  {
    id: "TEST_TOAST_NOTIFICATION2",
    weight: 100,
    template: "toast_notification",
    content: {
      title: "Launch action on toast click and on action button click",
      body: "Body",
      image_url:
        "https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/a3c640c8-7594-4bb2-bc18-8b4744f3aaf2.gif",
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
    trigger: { id: "backgroundTaskMessage" },
    frequency: { lifetime: 3 },
  },
  {
    id: "MR2022_BACKGROUND_UPDATE_TOAST_NOTIFICATION",
    weight: 100,
    template: "toast_notification",
    content: {
      title: {
        string_id: "mr2022-background-update-toast-title",
      },
      body: {
        string_id: "mr2022-background-update-toast-text",
      },
      image_url:
        "https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/673d2808-e5d8-41b9-957e-f60d53233b97.png",
      requireInteraction: true,
      actions: [
        {
          action: "open",
          title: {
            string_id: "mr2022-background-update-toast-primary-button-label",
          },
        },
        {
          action: "snooze",
          windowsSystemActivationType: true,
          title: {
            string_id: "mr2022-background-update-toast-secondary-button-label",
          },
        },
      ],
      tag: "mr2022_background_update",
    },
    groups: ["panel-test-provider"],
    targeting: "!hasActiveEnterprisePolicies",
    trigger: { id: "backgroundTaskMessage" },
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
          ],
          content: {
            position: "callout",
            title: { raw: "Panel Feature Callout" },
            subtitle: { raw: "Hello!" },
            secondary_button: {
              label: { raw: "Advance" },
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
    id: "FXA_ACCOUNTS_APPMENU_PROTECT_BROWSING_DATA",
    template: "menu_message",
    content: {
      messageType: "fxa_cta",
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
      imageURL:
        "chrome://browser/content/asrouter/assets/fox-with-box-on-cloud.svg",
      imageVerticalTopOffset: -20,
    },
    skip_in_tests: "TODO",
    trigger: {
      id: "menuOpened",
    },
    testingTriggerContext: "app_menu",
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
];

export const PanelTestProvider = {
  getMessages() {
    return Promise.resolve(
      MESSAGES().map(message => ({
        ...message,
        targeting: `providerCohorts.panel_local_testing == "SHOW_TEST"`,
      }))
    );
  },
};
