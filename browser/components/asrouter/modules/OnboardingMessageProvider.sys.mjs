/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// We use importESModule here instead of static import so that
// the Karma test environment won't choke on this module. This
// is because the Karma test environment already stubs out
// XPCOMUtils and AppConstants, and overrides importESModule
// to be a no-op (which can't be done for a static import statement).

// eslint-disable-next-line mozilla/use-static-import
const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

// eslint-disable-next-line mozilla/use-static-import
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

import { FeatureCalloutMessages } from "resource:///modules/asrouter/FeatureCalloutMessages.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  ShellService: "moz-src:///browser/components/shell/ShellService.sys.mjs",
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "usesFirefoxSync",
  "services.sync.username"
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "mobileDevices",
  "services.sync.clients.devices.mobile",
  0
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "hidePrivatePin",
  "browser.startup.upgradeDialog.pinPBM.disabled",
  false
);

const L10N = new Localization([
  "branding/brand.ftl",
  "browser/newtab/onboarding.ftl",
  "toolkit/branding/brandings.ftl",
]);

const HOMEPAGE_PREF = "browser.startup.homepage";
const NEWTAB_PREF = "browser.newtabpage.enabled";
const FOURTEEN_DAYS_IN_MS = 14 * 24 * 60 * 60 * 1000;
const isMSIX =
  AppConstants.platform === "win" &&
  Services.sysinfo.getProperty("hasWinPackageId", false);

const BASE_MESSAGES = () => [
  {
    id: "MENU_MESSAGE_DEFAULT_CTA_ILLUSTRATION_LAYOUT",
    template: "menu_message",
    layout: "column",
    content: {
      messageType: "default_cta",
      imageURL:
        "chrome://browser/content/asrouter/assets/fox-with-checkmark.svg",
      imageWidth: 68,
      primaryText: {
        string_id: "set-default-menu-message-row-layout-title",
      },
      secondaryText: {
        string_id: "set-default-menu-message-row-layout-subtitle",
      },
      primaryActionText: {
        string_id: "set-default-menu-message-primary-button-variant",
      },
      primaryButtonSize: "small",
      primaryAction: {
        type: "MULTI_ACTION",
        data: {
          actions: [
            {
              type: "SET_DEFAULT_BROWSER",
            },
          ],
        },
      },
      closeAction: {
        type: "BLOCK_MESSAGE",
        data: {
          id: "MENU_MESSAGE_DEFAULT_CTA_ILLUSTRATION_LAYOUT",
        },
      },
    },
    targeting:
      "source == 'app_menu' && os.isWindows && os.windowsVersion >= 10 && !isDefaultBrowserUncached && !hasActiveEnterprisePolicies && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue != false",
    trigger: {
      id: "menuOpened",
    },
    groups: [],
    skip_in_tests: "it's covered by browser_asrouter_menu_messages.js",
  },
  {
    id: "MENU_MESSAGE_DEFAULT_CTA_ILLUSTRATION_LAYOUT",
    template: "menu_message",
    layout: "column",
    content: {
      imageURL:
        "chrome://browser/content/asrouter/assets/fox-with-checkmark.svg",
      imageWidth: 68,
      messageType: "default_cta",
      primaryText: {
        string_id: "set-default-menu-message-row-layout-title",
      },
      secondaryText: {
        string_id: "set-default-menu-message-row-layout-subtitle-variant",
      },
      primaryActionText: {
        string_id: "set-default-menu-message-primary-button-variant",
      },
      primaryButtonSize: "small",
      primaryAction: {
        type: "MULTI_ACTION",
        data: {
          actions: [
            {
              type: "SET_DEFAULT_BROWSER",
            },
            {
              type: "PIN_FIREFOX_TO_TASKBAR",
            },
          ],
        },
      },
      closeAction: {
        type: "BLOCK_MESSAGE",
        data: {
          id: "MENU_MESSAGE_DEFAULT_CTA_ILLUSTRATION_LAYOUT",
        },
      },
    },
    targeting:
      "source == 'app_menu' && os.isMac && !isDefaultBrowserUncached && !hasActiveEnterprisePolicies && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue != false",
    trigger: {
      id: "menuOpened",
    },
    groups: [],
    skip_in_tests: "it's covered by browser_asrouter_menu_messages.js",
  },
  {
    id: "AI_WINDOW_TOU_EXISTING_USERS_MODAL",
    template: "spotlight",
    frequency: {
      lifetime: 100,
    },
    trigger: {
      id: "openURL",
      patterns: ["https://accounts.firefox.com/?*service=smartwindow*"],
    },
    targeting: `localeLanguageCode == 'en' && region in ['CA', 'US'] && !('termsofuse.bypassNotification'|preferenceValue) && ('termsofuse.acceptedVersion'|preferenceValue < 4) && ('browser.smartwindow.enabled'|preferenceValue)`,
    content: {
      template: "multistage",
      id: "AI_WINDOW_TOU_EXISTING_USERS_MODAL",
      modal: "window",
      requireAction: true,
      disableEscClose: true,
      screens: [
        {
          id: "AI_WINDOW_TOU_EXISTING_USERS_MODAL",
          content: {
            screen_style: {
              width: "585px",
            },
            logo: {
              height: "40px",
              width: "40",
            },
            title: {
              fontSize: "24px",
              string_id: "smartwindow-existing-user-fx-tou-title",
            },
            above_button_content: [
              {
                type: "text",
                text: {
                  string_id: "smartwindow-existing-user-fx-tou-body",
                  fontSize: "13px",
                  marginBlock: "10px 20px",
                  marginInline: "20px",
                },
                link_keys: ["terms_of_use", "privacy_notice"],
                font_styles: "legal",
              },
            ],
            terms_of_use: {
              action: {
                type: "OPEN_URL",
                data: {
                  where: "chromeless",
                  args: "https://www.mozilla.org/about/legal/terms/firefox/",
                },
              },
            },
            privacy_notice: {
              action: {
                type: "OPEN_URL",
                data: {
                  where: "chromeless",
                  args: "https://www.mozilla.org/privacy/firefox/",
                },
              },
            },
            additional_button: {
              label: {
                string_id: "smartwindow-existing-user-fx-tou-accept",
                paddingBlock: "4px",
              },
              style: "primary",
              flow: "row",
              action: {
                type: "MULTI_ACTION",
                navigate: true,
                data: {
                  actions: [
                    {
                      type: "SET_PREF",
                      data: {
                        pref: {
                          name: "termsofuse.acceptedVersion",
                          value: 4,
                        },
                      },
                    },
                    {
                      type: "SET_PREF",
                      data: {
                        pref: {
                          name: "termsofuse.acceptedDate",
                          value: {
                            timestamp: "true",
                          },
                        },
                      },
                    },
                  ],
                },
              },
            },
            primary_button: {
              label: {
                string_id: "smartwindow-existing-user-fx-tou-go-back",
                paddingBlock: "4px",
              },
              style: "secondary",
              flow: "row",
              action: {
                type: "MULTI_ACTION",
                dismiss: true,
                data: {
                  actions: [
                    {
                      type: "OPEN_ABOUT_PAGE",
                      data: {
                        args: "newtab",
                        where: "current",
                      },
                    },
                  ],
                },
              },
            },
          },
        },
      ],
    },
  },
  {
    id: "BROWSER_BACKUP_OPTIN_SPOTLIGHT",
    groups: ["win10-eos-sync", "eco"],
    // TODO: The backup preferences in this expression should be updated once BackupService exposes getters; see Bug 1993272
    targeting:
      "source == 'newtab' && os.isWindows && os.windowsVersion == 10 && os.windowsBuildNumber <= 19045 && isFxAEnabled && !isFxASignedIn && !hasSelectableProfiles && !hasActiveEnterprisePolicies && backupArchiveEnabled && (!'browser.backup.scheduled.enabled' | preferenceValue) && (!'browser.backup.scheduled.user-disabled' | preferenceValue) && !isMajorUpgrade && !willShowDefaultPrompt && !activeNotifications && previousSessionEnd && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue != false",
    trigger: {
      id: "defaultBrowserCheck",
    },
    template: "spotlight",
    priority: 5,
    frequency: {
      lifetime: 1,
    },
    content: {
      id: "BROWSER_BACKUP_OPTIN_SPOTLIGHT",
      template: "multistage",
      modal: "tab",
      transitions: true,
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
                      "center / contain no-repeat url('https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/733144c8-a453-49eb-aff7-27a10786fbc1.svg')",
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
                      top: "revert",
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
                      "center / contain no-repeat url('https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/112b3d3c-5f6b-42c1-b56b-c70b08a6e4ad.svg')",
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
            additional_button: {
              label: {
                string_id: "fx-view-discoverability-secondary-button-label",
                fontSize: "0.75em",
                minHeight: "24px",
                minWidth: "revert",
                lineHeight: "100%",
                paddingBlock: "4px",
                paddingInline: "12px",
              },
              style: "secondary",
              flow: "row",
              action: {
                type: "BLOCK_MESSAGE",
                data: {
                  id: "BROWSER_BACKUP_OPTIN_SPOTLIGHT",
                },
                dismiss: true,
              },
            },
            submenu_button: {
              label: {
                minHeight: "24px",
                minWidth: "24px",
                paddingBlock: "0",
                paddingInline: "0",
              },
              submenu: [
                {
                  type: "action",
                  label: { string_id: "create-backup-show-fewer" },
                  action: {
                    type: "MULTI_ACTION",
                    dismiss: true,
                    data: {
                      actions: [
                        {
                          type: "SET_PREF",
                          data: {
                            pref: {
                              name: "messaging-system-action.show-fewer-backup-messages",
                              value: true,
                            },
                          },
                        },
                        {
                          type: "BLOCK_MESSAGE",
                          data: {
                            id: "BROWSER_BACKUP_OPTIN_SPOTLIGHT",
                          },
                        },
                      ],
                    },
                  },
                  id: "show_fewer_recommendations",
                },
              ],
              attached_to: "additional_button",
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
                      "center / contain no-repeat url('https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/1741e2ae-2423-4b74-9f3b-b22dcd48d3b3.svg')",
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
                      "center / contain no-repeat url('https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/0ddfd632-b9c4-45d6-86c3-b89f94797110.svg')",
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
                "https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/0706f067-eaf8-4537-a9e1-6098d990f511.svg",
              height: "110px",
            },
            title: {
              string_id: "create-backup-screen-3-location",
              paddingBlock: "8px",
              fontSize: "24px",
              fontWeight: 600,
            },
            isEncryptedBackup: false,
            screen_style: {
              width: "650px",
              height: "600px",
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
          id: "SCREEN_3B",
          force_hide_steps_indicator: true,
          targeting: "isEncryptedBackup",
          content: {
            isEncryptedBackup: true,
            logo: {
              imageURL:
                "https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/0706f067-eaf8-4537-a9e1-6098d990f511.svg",
              height: "110px",
            },
            title: {
              string_id: "create-backup-screen-3-location",
            },
            screen_style: {
              width: "650px",
              height: "600px",
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
          id: "FX_BACKUP_ENCRYPTION",
          force_hide_steps_indicator: true,
          targeting: "isEncryptedBackup",
          content: {
            isEncryptedBackup: true,
            title: {
              string_id: "create-backup-screen-3-title",
            },
            subtitle: {
              string_id: "create-backup-screen-3-subtitle",
              fontSize: "13px",
            },
            screen_style: {
              width: "700px",
              height: "650px",
            },
            logo: {
              imageURL:
                "https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/ms-images/0fb332a4-6b15-4d6e-bbd5-0558ac3e004f.svg",
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
          id: "BACKUP_CONFIRMATION_SCREEN_EASY",
          force_hide_steps_indicator: true,
          targeting: "!isEncryptedBackup",
          content: {
            screen_style: {
              width: "664px",
              height: "580px",
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
                      fontWeight: "600",
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
                      fontWeight: "600",
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
                      fontWeight: "600",
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
                dismiss: true,
              },
            },
            additional_button: {
              label: {
                string_id: "fx-backup-confirmation-screen-close-button",
                fontSize: "0.75em",
                minHeight: "24px",
                minWidth: "revert",
                lineHeight: "100%",
                paddingBlock: "4px",
                paddingInline: "12px",
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
              height: "580px",
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
                      fontWeight: "600",
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
                      fontWeight: "600",
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
                      fontWeight: "600",
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
                dismiss: true,
              },
            },
            additional_button: {
              label: {
                string_id: "fx-backup-confirmation-screen-close-button",
                fontSize: "0.75em",
                minHeight: "24px",
                minWidth: "revert",
                lineHeight: "100%",
                paddingBlock: "4px",
                paddingInline: "12px",
              },
              style: "secondary",
              action: { dismiss: true },
            },
          },
        },
      ],
    },
  },
  {
    id: "FXA_ACCOUNTS_BADGE",
    template: "toolbar_badge",
    content: {
      delay: 10000, // delay for 10 seconds
      target: "fxa-toolbar-menu-button",
    },
    targeting: "false",
    trigger: { id: "toolbarBadgeUpdate" },
  },
  {
    id: "MILESTONE_MESSAGE_87",
    groups: ["cfr"],
    content: {
      text: "",
      layout: "short_message",
      buttons: {
        primary: {
          event: "PROTECTION",
          label: {
            string_id: "cfr-doorhanger-milestone-ok-button",
          },
          action: {
            type: "OPEN_PROTECTION_REPORT",
          },
        },
        secondary: [
          {
            event: "DISMISS",
            label: {
              string_id: "cfr-doorhanger-milestone-close-button",
            },
            action: {
              type: "CANCEL",
            },
          },
        ],
      },
      category: "cfrFeatures",
      anchor_id: "tracking-protection-icon-container",
      bucket_id: "CFR_MILESTONE_MESSAGE",
      heading_text: {
        string_id: "cfr-doorhanger-milestone-heading2",
      },
      notification_text: "",
      skip_address_bar_notifier: true,
    },
    trigger: {
      id: "contentBlocking",
      params: ["ContentBlockingMilestone"],
    },
    template: "milestone_message",
    frequency: {
      lifetime: 7,
    },
    targeting: "pageLoad >= 4 && userPrefs.cfrFeatures",
  },
  {
    id: "FX_MR_106_UPGRADE",
    template: "spotlight",
    targeting: "true",
    content: {
      template: "multistage",
      id: "FX_MR_106_UPGRADE",
      transitions: true,
      modal: "tab",
      screens: [
        {
          id: "UPGRADE_PIN_FIREFOX",
          content: {
            position: "split",
            split_narrow_bkg_position: "-155px",
            image_alt_text: {
              string_id: "mr2022-onboarding-pin-image-alt",
            },
            progress_bar: "true",
            background:
              "url('chrome://activity-stream/content/data/content/assets/mr-pintaskbar.svg') var(--mr-secondary-position) no-repeat var(--mr-screen-background-color)",
            logo: {},
            title: {
              string_id: "mr2022-onboarding-existing-pin-header",
            },
            subtitle: {
              string_id: "mr2022-onboarding-existing-pin-subtitle",
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
            checkbox: {
              label: {
                string_id: "mr2022-onboarding-existing-pin-checkbox-label",
              },
              defaultValue: true,
              action: {
                type: "MULTI_ACTION",
                navigate: true,
                data: {
                  actions: [
                    {
                      type: "PIN_FIREFOX_TO_TASKBAR",
                      data: {
                        privatePin: true,
                      },
                    },
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
                string_id: "mr2022-onboarding-secondary-skip-button-label",
              },
              action: {
                navigate: true,
              },
              has_arrow_icon: true,
            },
          },
        },
        {
          id: "UPGRADE_SET_DEFAULT",
          content: {
            position: "split",
            split_narrow_bkg_position: "-60px",
            image_alt_text: {
              string_id: "mr2022-onboarding-default-image-alt",
            },
            progress_bar: "true",
            background:
              "url('chrome://activity-stream/content/data/content/assets/mr-settodefault.svg') var(--mr-secondary-position) no-repeat var(--mr-screen-background-color)",
            logo: {},
            title: {
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
                string_id: "mr2022-onboarding-secondary-skip-button-label",
              },
              action: {
                navigate: true,
              },
              has_arrow_icon: true,
            },
          },
        },
        {
          id: "UPGRADE_IMPORT_SETTINGS_EMBEDDED",
          content: {
            tiles: { type: "migration-wizard" },
            position: "split",
            split_narrow_bkg_position: "-42px",
            image_alt_text: {
              string_id: "mr2022-onboarding-import-image-alt",
            },
            background:
              "url('chrome://activity-stream/content/data/content/assets/mr-import.svg') var(--mr-secondary-position) no-repeat var(--mr-screen-background-color)",
            progress_bar: true,
            hide_secondary_section: "responsive",
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
        {
          id: "UPGRADE_MOBILE_DOWNLOAD",
          content: {
            position: "split",
            split_narrow_bkg_position: "-160px",
            image_alt_text: {
              string_id: "mr2022-onboarding-mobile-download-image-alt",
            },
            background:
              "url('chrome://activity-stream/content/data/content/assets/mr-mobilecrosspromo.svg') var(--mr-secondary-position) no-repeat var(--mr-screen-background-color)",
            progress_bar: true,
            logo: {},
            title: {
              string_id:
                "onboarding-mobile-download-security-and-privacy-title",
            },
            subtitle: {
              string_id:
                "onboarding-mobile-download-security-and-privacy-subtitle",
            },
            hero_image: {
              url: "chrome://activity-stream/content/data/content/assets/mobile-download-qr-existing-user.svg",
            },
            cta_paragraph: {
              text: {
                string_id: "mr2022-onboarding-mobile-download-cta-text",
                string_name: "download-label",
              },
              action: {
                type: "OPEN_URL",
                data: {
                  args: "https://www.mozilla.org/firefox/mobile/get-app/?utm_medium=firefox-desktop&utm_source=onboarding-modal&utm_campaign=mr2022&utm_content=existing-global",
                  where: "tab",
                },
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
        {
          id: "UPGRADE_PIN_PRIVATE_WINDOW",
          content: {
            position: "split",
            split_narrow_bkg_position: "-100px",
            image_alt_text: {
              string_id: "mr2022-onboarding-pin-private-image-alt",
            },
            progress_bar: "true",
            background:
              "url('chrome://activity-stream/content/data/content/assets/mr-pinprivate.svg') var(--mr-secondary-position) no-repeat var(--mr-screen-background-color)",
            logo: {},
            title: {
              string_id: "mr2022-upgrade-onboarding-pin-private-window-header",
            },
            subtitle: {
              string_id:
                "mr2022-upgrade-onboarding-pin-private-window-subtitle",
            },
            primary_button: {
              label: {
                string_id:
                  "mr2022-upgrade-onboarding-pin-private-window-primary-button-label",
              },
              action: {
                type: "PIN_FIREFOX_TO_TASKBAR",
                data: {
                  privatePin: true,
                },
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
        {
          id: "UPGRADE_DATA_RECOMMENDATION",
          content: {
            position: "split",
            split_narrow_bkg_position: "-80px",
            image_alt_text: {
              string_id: "mr2022-onboarding-privacy-segmentation-image-alt",
            },
            progress_bar: "true",
            background:
              "url('chrome://activity-stream/content/data/content/assets/mr-privacysegmentation.svg') var(--mr-secondary-position) no-repeat var(--mr-screen-background-color)",
            logo: {},
            title: {
              string_id: "mr2022-onboarding-privacy-segmentation-title",
            },
            subtitle: {
              string_id: "mr2022-onboarding-privacy-segmentation-subtitle",
            },
            cta_paragraph: {
              text: {
                string_id: "mr2022-onboarding-privacy-segmentation-text-cta",
              },
            },
            primary_button: {
              label: {
                string_id:
                  "mr2022-onboarding-privacy-segmentation-button-primary-label",
              },
              action: {
                type: "SET_PREF",
                data: {
                  pref: {
                    name: "browser.dataFeatureRecommendations.enabled",
                    value: true,
                  },
                },
                navigate: true,
              },
            },
            additional_button: {
              label: {
                string_id:
                  "mr2022-onboarding-privacy-segmentation-button-secondary-label",
              },
              style: "secondary",
              action: {
                type: "SET_PREF",
                data: {
                  pref: {
                    name: "browser.dataFeatureRecommendations.enabled",
                    value: false,
                  },
                },
                navigate: true,
              },
            },
          },
        },
        {
          id: "UPGRADE_GRATITUDE",
          content: {
            position: "split",
            progress_bar: "true",
            split_narrow_bkg_position: "-228px",
            image_alt_text: {
              string_id: "mr2022-onboarding-gratitude-image-alt",
            },
            background:
              "url('chrome://activity-stream/content/data/content/assets/mr-gratitude.svg') var(--mr-secondary-position) no-repeat var(--mr-screen-background-color)",
            logo: {},
            title: {
              string_id: "mr2022-onboarding-gratitude-title",
            },
            subtitle: {
              string_id: "mr2022-onboarding-gratitude-subtitle",
            },
            primary_button: {
              label: {
                string_id: "mr2022-onboarding-gratitude-primary-button-label",
              },
              action: {
                type: "OPEN_FIREFOX_VIEW",
                navigate: true,
              },
            },
            secondary_button: {
              label: {
                string_id: "mr2022-onboarding-gratitude-secondary-button-label",
              },
              action: {
                navigate: true,
              },
            },
          },
        },
      ],
    },
  },
  {
    id: "FX_100_UPGRADE",
    template: "spotlight",
    targeting: "false",
    content: {
      template: "multistage",
      id: "FX_100_UPGRADE",
      transitions: true,
      screens: [
        {
          id: "UPGRADE_PIN_FIREFOX",
          content: {
            logo: {
              imageURL:
                "chrome://activity-stream/content/data/content/assets/heart.webp",
              height: "73px",
            },
            has_noodles: true,
            title: {
              fontSize: "36px",
              string_id: "fx100-upgrade-thanks-header",
            },
            title_style: "fancy shine",
            background:
              "url('chrome://activity-stream/content/data/content/assets/confetti.svg') top / 100% no-repeat var(--background-color-canvas)",
            subtitle: {
              string_id: "fx100-upgrade-thanks-keep-body",
            },
            primary_button: {
              label: {
                string_id: "fx100-thank-you-pin-primary-button-label",
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
          },
        },
      ],
    },
  },
  {
    id: "PB_NEWTAB_FOCUS_PROMO",
    type: "default",
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
      promoType: "FOCUS",
      promoHeader: "fluent:about-private-browsing-focus-promo-header-c",
      promoImageLarge: "chrome://browser/content/assets/focus-promo.png",
      promoLinkText: "fluent:about-private-browsing-focus-promo-cta",
      promoLinkType: "button",
      promoSectionStyle: "below-search",
      promoTitle: "fluent:about-private-browsing-focus-promo-text-c",
      promoTitleEnabled: true,
      promoButton: {
        action: {
          type: "SHOW_SPOTLIGHT",
          data: {
            content: {
              id: "FOCUS_PROMO",
              template: "multistage",
              modal: "tab",
              backdrop: "transparent",
              screens: [
                {
                  id: "DEFAULT_MODAL_UI",
                  content: {
                    logo: {
                      imageURL:
                        "chrome://browser/content/assets/focus-logo.svg",
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
                        navigate: true,
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
                        marketplace_buttons: ["ios", "android"],
                      },
                    },
                  },
                },
              ],
            },
          },
        },
      },
    },
    priority: 2,
    frequency: {
      custom: [
        {
          cap: 3,
          period: 604800000, // Max 3 per week
        },
      ],
      lifetime: 12,
    },
    // Exclude the next 2 messages: 1) Klar for en 2) Klar for de
    targeting:
      "!(region in [ 'DE', 'AT', 'CH'] && localeLanguageCode == 'en') && localeLanguageCode != 'de'",
  },
  {
    id: "PB_NEWTAB_KLAR_PROMO",
    type: "default",
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
      promoType: "FOCUS",
      promoHeader: "fluent:about-private-browsing-focus-promo-header-c",
      promoImageLarge: "chrome://browser/content/assets/focus-promo.png",
      promoLinkText: "Download Firefox Klar",
      promoLinkType: "button",
      promoSectionStyle: "below-search",
      promoTitle:
        "Firefox Klar clears your history every time while blocking ads and trackers.",
      promoTitleEnabled: true,
      promoButton: {
        action: {
          type: "SHOW_SPOTLIGHT",
          data: {
            content: {
              id: "KLAR_PROMO",
              template: "multistage",
              modal: "tab",
              backdrop: "transparent",
              screens: [
                {
                  id: "DEFAULT_MODAL_UI",
                  order: 0,
                  content: {
                    logo: {
                      imageURL:
                        "chrome://browser/content/assets/focus-logo.svg",
                      height: "48px",
                    },
                    title: "Get Firefox Klar",
                    subtitle: {
                      string_id: "spotlight-focus-promo-subtitle",
                    },
                    dismiss_button: {
                      action: {
                        navigate: true,
                      },
                    },
                    ios: {
                      action: {
                        data: {
                          args: "https://app.adjust.com/a8bxj8j?campaign=firefox-desktop&adgroup=pb&creative=focus-omc172&redirect=https%3A%2F%2Fapps.apple.com%2Fde%2Fapp%2Fklar-by-firefox%2Fid1073435754",
                          where: "tabshifted",
                        },
                        type: "OPEN_URL",
                        navigate: true,
                      },
                    },
                    android: {
                      action: {
                        data: {
                          args: "https://app.adjust.com/a8bxj8j?campaign=firefox-desktop&adgroup=pb&creative=focus-omc172&redirect=https%3A%2F%2Fplay.google.com%2Fstore%2Fapps%2Fdetails%3Fid%3Dorg.mozilla.klar",
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
                            "chrome://browser/content/assets/klar-qr-code.svg",
                          alt_text: "Scan the QR code to get Firefox Klar",
                        },
                        marketplace_buttons: ["ios", "android"],
                      },
                    },
                  },
                },
              ],
            },
          },
        },
      },
    },
    priority: 2,
    frequency: {
      custom: [
        {
          cap: 3,
          period: 604800000, // Max 3 per week
        },
      ],
      lifetime: 12,
    },
    targeting: "region in [ 'DE', 'AT', 'CH'] && localeLanguageCode == 'en'",
  },
  {
    id: "PB_NEWTAB_KLAR_PROMO_DE",
    type: "default",
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
      promoType: "FOCUS",
      promoHeader: "fluent:about-private-browsing-focus-promo-header-c",
      promoImageLarge: "chrome://browser/content/assets/focus-promo.png",
      promoLinkText: "fluent:about-private-browsing-focus-promo-cta",
      promoLinkType: "button",
      promoSectionStyle: "below-search",
      promoTitle: "fluent:about-private-browsing-focus-promo-text-c",
      promoTitleEnabled: true,
      promoButton: {
        action: {
          type: "SHOW_SPOTLIGHT",
          data: {
            content: {
              id: "FOCUS_PROMO",
              template: "multistage",
              modal: "tab",
              backdrop: "transparent",
              screens: [
                {
                  id: "DEFAULT_MODAL_UI",
                  content: {
                    logo: {
                      imageURL:
                        "chrome://browser/content/assets/focus-logo.svg",
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
                        navigate: true,
                      },
                    },
                    ios: {
                      action: {
                        data: {
                          args: "https://app.adjust.com/a8bxj8j?campaign=firefox-desktop&adgroup=pb&creative=focus-omc172&redirect=https%3A%2F%2Fapps.apple.com%2Fde%2Fapp%2Fklar-by-firefox%2Fid1073435754",
                          where: "tabshifted",
                        },
                        type: "OPEN_URL",
                        navigate: true,
                      },
                    },
                    android: {
                      action: {
                        data: {
                          args: "https://app.adjust.com/a8bxj8j?campaign=firefox-desktop&adgroup=pb&creative=focus-omc172&redirect=https%3A%2F%2Fplay.google.com%2Fstore%2Fapps%2Fdetails%3Fid%3Dorg.mozilla.klar",
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
                            "chrome://browser/content/assets/klar-qr-code.svg",
                          alt_text: {
                            string_id: "spotlight-focus-promo-qr-code",
                          },
                        },
                        marketplace_buttons: ["ios", "android"],
                      },
                    },
                  },
                },
              ],
            },
          },
        },
      },
    },
    priority: 2,
    frequency: {
      custom: [
        {
          cap: 3,
          period: 604800000, // Max 3 per week
        },
      ],
      lifetime: 12,
    },
    targeting: "localeLanguageCode == 'de'",
  },
  {
    id: "PB_NEWTAB_PIN_PROMO",
    template: "pb_newtab",
    type: "default",
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
      promoHeader: "fluent:about-private-browsing-pin-promo-header",
      promoImageLarge:
        "chrome://browser/content/assets/private-promo-asset.svg",
      promoLinkText: "fluent:about-private-browsing-pin-promo-link-text",
      promoLinkType: "button",
      promoSectionStyle: "below-search",
      promoTitle: "fluent:about-private-browsing-pin-promo-title",
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
                data: {
                  privatePin: true,
                },
              },
              {
                type: "BLOCK_MESSAGE",
                data: {
                  id: "PB_NEWTAB_PIN_PROMO",
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
    targeting: "doesAppNeedPrivatePin",
  },
  {
    id: "PB_NEWTAB_COOKIE_BANNERS_PROMO",
    template: "pb_newtab",
    type: "default",
    groups: ["pbNewtab"],
    content: {
      infoBody: "fluent:about-private-browsing-info-description-simplified",
      infoEnabled: true,
      infoIcon: "chrome://global/skin/icons/indicator-private-browsing.svg",
      infoLinkText: "fluent:about-private-browsing-learn-more-link",
      infoTitle: "",
      infoTitleEnabled: false,
      promoEnabled: true,
      promoType: "COOKIE_BANNERS",
      promoHeader: "fluent:about-private-browsing-cookie-banners-promo-heading",
      promoImageLarge:
        "chrome://browser/content/assets/cookie-banners-begone.svg",
      promoLinkText: "fluent:about-private-browsing-learn-more-link",
      promoLinkType: "link",
      promoSectionStyle: "below-search",
      promoTitle: "fluent:about-private-browsing-cookie-banners-promo-body",
      promoTitleEnabled: true,
      promoButton: {
        action: {
          type: "MULTI_ACTION",
          data: {
            actions: [
              {
                type: "OPEN_URL",
                data: {
                  args: "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/cookie-banner-reduction",
                  where: "tabshifted",
                },
              },
              {
                type: "BLOCK_MESSAGE",
                data: {
                  id: "PB_NEWTAB_COOKIE_BANNERS_PROMO",
                },
              },
            ],
          },
        },
      },
    },
    priority: 4,
    frequency: {
      custom: [
        {
          cap: 3,
          period: 604800000, // Max 3 per week
        },
      ],
      lifetime: 12,
    },
    targeting: `'cookiebanners.service.mode.privateBrowsing'|preferenceValue != 0 || 'cookiebanners.service.mode'|preferenceValue != 0`,
  },
  {
    id: "INFOBAR_LAUNCH_ON_LOGIN",
    groups: ["cfr"],
    template: "infobar",
    content: {
      type: "global",
      text: {
        string_id: "launch-on-login-infobar-message",
      },
      buttons: [
        {
          label: {
            string_id: "launch-on-login-learnmore",
          },
          supportPage: "make-firefox-automatically-open-when-you-start",
          action: {
            type: "CANCEL",
          },
        },
        {
          label: { string_id: "launch-on-login-infobar-reject-button" },
          action: {
            type: "CANCEL",
          },
        },
        {
          label: { string_id: "launch-on-login-infobar-confirm-button" },
          primary: true,
          action: {
            type: "MULTI_ACTION",
            data: {
              actions: [
                {
                  type: "SET_PREF",
                  data: {
                    pref: {
                      name: "browser.startup.windowsLaunchOnLogin.disableLaunchOnLoginPrompt",
                      value: true,
                    },
                  },
                },
                {
                  type: "CONFIRM_LAUNCH_ON_LOGIN",
                },
              ],
            },
          },
        },
      ],
    },
    frequency: {
      lifetime: 1,
    },
    trigger: { id: "defaultBrowserCheck" },
    targeting: `source == 'newtab'
    && 'browser.startup.windowsLaunchOnLogin.disableLaunchOnLoginPrompt'|preferenceValue == false
    && 'browser.startup.windowsLaunchOnLogin.enabled'|preferenceValue == true && isDefaultBrowser && !activeNotifications
    && !launchOnLoginEnabled`,
  },
  {
    id: "INFOBAR_LAUNCH_ON_LOGIN_FINAL",
    groups: ["cfr"],
    template: "infobar",
    content: {
      type: "global",
      text: {
        string_id: "launch-on-login-infobar-final-message",
      },
      buttons: [
        {
          label: {
            string_id: "launch-on-login-learnmore",
          },
          supportPage: "make-firefox-automatically-open-when-you-start",
          action: {
            type: "CANCEL",
          },
        },
        {
          label: { string_id: "launch-on-login-infobar-final-reject-button" },
          action: {
            type: "SET_PREF",
            data: {
              pref: {
                name: "browser.startup.windowsLaunchOnLogin.disableLaunchOnLoginPrompt",
                value: true,
              },
            },
          },
        },
        {
          label: { string_id: "launch-on-login-infobar-confirm-button" },
          primary: true,
          action: {
            type: "MULTI_ACTION",
            data: {
              actions: [
                {
                  type: "SET_PREF",
                  data: {
                    pref: {
                      name: "browser.startup.windowsLaunchOnLogin.disableLaunchOnLoginPrompt",
                      value: true,
                    },
                  },
                },
                {
                  type: "CONFIRM_LAUNCH_ON_LOGIN",
                },
              ],
            },
          },
        },
      ],
    },
    frequency: {
      lifetime: 1,
    },
    trigger: { id: "defaultBrowserCheck" },
    targeting: `source == 'newtab'
    && 'browser.startup.windowsLaunchOnLogin.disableLaunchOnLoginPrompt'|preferenceValue == false
    && 'browser.startup.windowsLaunchOnLogin.enabled'|preferenceValue == true && isDefaultBrowser && !activeNotifications
    && messageImpressions.INFOBAR_LAUNCH_ON_LOGIN[messageImpressions.INFOBAR_LAUNCH_ON_LOGIN | length - 1]
    && messageImpressions.INFOBAR_LAUNCH_ON_LOGIN[messageImpressions.INFOBAR_LAUNCH_ON_LOGIN | length - 1] <
      currentDate|date - ${FOURTEEN_DAYS_IN_MS}
    && !launchOnLoginEnabled`,
  },
  {
    id: "RESTORE_FROM_BACKUP",
    template: "spotlight",
    groups: [""],
    content: {
      template: "multistage",
      transitions: true,
      modal: "tab",
      backdrop: "transparent",
      id: "RESTORE_FROM_BACKUP",
      screens: [
        {
          id: "RESTORE_FROM_BACKUP_SCREEN",
          content: {
            position: "split",
            split_content_padding_block: "166px",
            background:
              "url('chrome://activity-stream/content/data/content/assets/fox-doodle-backup.svg') var(--mr-secondary-position) no-repeat var(--mr-screen-background-color)",
            logo: {},
            title: {
              string_id: "restored-from-backup-success-title",
            },
            primary_button: {
              label: {
                string_id:
                  "restored-from-backup-success-no-checklist-primary-button",
                paddingBlock: "4px",
                paddingInline: "16px",
              },
              action: {
                navigate: true,
              },
            },
          },
        },
      ],
    },
    targeting:
      "source == 'startup' && !doesAppNeedPin && (!'browser.shell.checkDefaultBrowser'|preferenceValue || isDefaultBrowser) && !willShowDefaultPrompt && 'browser.backup.profile-restoration-date'|preferenceValue && !'browser.profiles.profile-copied'|preferenceValue",
    trigger: {
      id: "defaultBrowserCheck",
    },
    frequency: {
      lifetime: 1,
    },
  },
  {
    id: "RESTORE_FROM_BACKUP_NEED_DEFAULT_NEED_PIN",
    template: "spotlight",
    groups: [""],
    content: {
      template: "multistage",
      transitions: true,
      modal: "tab",
      backdrop: "transparent",
      id: "RESTORE_FROM_BACKUP_NEED_DEFAULT_NEED_PIN",
      screens: [
        {
          id: "RESTORE_FROM_BACKUP_NEED_DEFAULT_NEED_PIN_SCREEN",
          content: {
            position: "split",
            background:
              "url('chrome://activity-stream/content/data/content/assets/fox-doodle-backup.svg') var(--mr-secondary-position) no-repeat var(--mr-screen-background-color)",
            logo: {},
            title: {
              string_id: "restored-from-backup-success-title",
            },
            subtitle: {
              string_id: "restored-from-backup-success-with-checklist-subtitle",
              paddingInline: "0 48px",
            },
            primary_button: {
              label: {
                string_id:
                  "restored-from-backup-success-with-checklist-primary-button",
                paddingBlock: "4px",
                paddingInline: "16px",
              },
              action: {
                type: "MULTI_ACTION",
                collectSelect: true,
                navigate: true,
                data: {
                  actions: [],
                },
              },
            },
            secondary_button: {
              label: {
                string_id:
                  "restored-from-backup-success-with-checklist-secondary-button",
              },
              action: {
                navigate: true,
              },
              has_arrow_icon: true,
            },
            tiles: {
              type: "multiselect",
              style: {
                gap: "10px",
              },
              data: [
                {
                  id: "checkbox-1",
                  defaultValue: true,
                  label: {
                    string_id:
                      "mr2022-onboarding-easy-setup-set-default-checkbox-label",
                  },
                  action: {
                    type: "SET_DEFAULT_BROWSER",
                  },
                },
                {
                  id: "checkbox-2",
                  defaultValue: true,
                  label: {
                    string_id: isMSIX
                      ? "mr2022-onboarding-pin-primary-button-label-msix"
                      : "mr2022-onboarding-pin-primary-button-label",
                  },
                  action: {
                    type: "MULTI_ACTION",
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
              ],
            },
          },
        },
      ],
    },
    targeting:
      "source == 'startup' && doesAppNeedPin && 'browser.shell.checkDefaultBrowser'|preferenceValue && !isDefaultBrowser && !willShowDefaultPrompt && 'browser.backup.profile-restoration-date'|preferenceValue && !'browser.profiles.profile-copied'|preferenceValue",
    trigger: {
      id: "defaultBrowserCheck",
    },
    frequency: {
      lifetime: 1,
    },
  },
  {
    id: "RESTORE_FROM_BACKUP_NEED_DEFAULT",
    template: "spotlight",
    groups: [""],
    content: {
      template: "multistage",
      transitions: true,
      modal: "tab",
      backdrop: "transparent",
      id: "RESTORE_FROM_BACKUP_NEED_DEFAULT",
      screens: [
        {
          id: "RESTORE_FROM_BACKUP_NEED_DEFAULT_SCREEN",
          content: {
            position: "split",
            background:
              "url('chrome://activity-stream/content/data/content/assets/fox-doodle-backup.svg') var(--mr-secondary-position) no-repeat var(--mr-screen-background-color)",
            logo: {},
            title: {
              string_id: "restored-from-backup-success-title",
            },
            subtitle: {
              string_id: "restored-from-backup-success-with-checklist-subtitle",
              paddingInline: "0 48px",
            },
            primary_button: {
              label: {
                string_id:
                  "restored-from-backup-success-with-checklist-primary-button",
                paddingBlock: "4px",
                paddingInline: "16px",
              },
              action: {
                type: "MULTI_ACTION",
                collectSelect: true,
                navigate: true,
                data: {
                  actions: [],
                },
              },
            },
            secondary_button: {
              label: {
                string_id:
                  "restored-from-backup-success-with-checklist-secondary-button",
              },
              action: {
                navigate: true,
              },
              has_arrow_icon: true,
            },
            tiles: {
              type: "multiselect",
              style: {
                gap: "10px",
              },
              data: [
                {
                  id: "checkbox-1",
                  defaultValue: true,
                  label: {
                    string_id:
                      "mr2022-onboarding-easy-setup-set-default-checkbox-label",
                  },
                  action: {
                    type: "SET_DEFAULT_BROWSER",
                  },
                },
              ],
            },
          },
        },
      ],
    },
    targeting:
      "source == 'startup' && !doesAppNeedPin && 'browser.shell.checkDefaultBrowser'|preferenceValue && !isDefaultBrowser && !willShowDefaultPrompt && 'browser.backup.profile-restoration-date'|preferenceValue && !'browser.profiles.profile-copied'|preferenceValue",
    trigger: {
      id: "defaultBrowserCheck",
    },
    frequency: {
      lifetime: 1,
    },
  },
  {
    id: "RESTORE_FROM_BACKUP_NEED_PIN",
    template: "spotlight",
    groups: [""],
    content: {
      template: "multistage",
      transitions: true,
      modal: "tab",
      backdrop: "transparent",
      id: "RESTORE_FROM_BACKUP_NEED_PIN",
      screens: [
        {
          id: "RESTORE_FROM_BACKUP_NEED_PIN_SCREEN",
          content: {
            position: "split",
            background:
              "url('chrome://activity-stream/content/data/content/assets/fox-doodle-backup.svg') var(--mr-secondary-position) no-repeat var(--mr-screen-background-color)",
            logo: {},
            title: {
              string_id: "restored-from-backup-success-title",
            },
            subtitle: {
              string_id: "restored-from-backup-success-with-checklist-subtitle",
              paddingInline: "0 48px",
            },
            primary_button: {
              label: {
                string_id:
                  "restored-from-backup-success-with-checklist-primary-button",
                paddingBlock: "4px",
                paddingInline: "16px",
              },
              action: {
                type: "MULTI_ACTION",
                collectSelect: true,
                navigate: true,
                data: {
                  actions: [],
                },
              },
            },
            secondary_button: {
              label: {
                string_id:
                  "restored-from-backup-success-with-checklist-secondary-button",
              },
              action: {
                navigate: true,
              },
              has_arrow_icon: true,
            },
            tiles: {
              type: "multiselect",
              style: {
                gap: "10px",
              },
              data: [
                {
                  id: "checkbox-1",
                  defaultValue: true,
                  label: {
                    string_id: isMSIX
                      ? "mr2022-onboarding-pin-primary-button-label-msix"
                      : "mr2022-onboarding-pin-primary-button-label",
                  },
                  action: {
                    type: "MULTI_ACTION",
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
              ],
            },
          },
        },
      ],
    },
    targeting:
      "source == 'startup' && doesAppNeedPin && !willShowDefaultPrompt &&(!'browser.shell.checkDefaultBrowser'|preferenceValue || isDefaultBrowser) && 'browser.backup.profile-restoration-date'|preferenceValue && !'browser.profiles.profile-copied'|preferenceValue",
    trigger: {
      id: "defaultBrowserCheck",
    },
    frequency: {
      lifetime: 1,
    },
  },
  {
    id: "SET_DEFAULT_BROWSER_GUIDANCE_NOTIFICATION_WIN10",
    template: "toast_notification",
    content: {
      title: {
        string_id: "default-browser-guidance-notification-title",
      },
      body: {
        string_id:
          "default-browser-guidance-notification-body-instruction-win10",
      },
      launch_action: {
        type: "OPEN_URL",
        data: {
          args: "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/win-set-firefox-default-browser",
          where: "tabshifted",
        },
      },
      requireInteraction: true,
      actions: [
        {
          action: "info-page",
          title: {
            string_id: "default-browser-guidance-notification-info-page",
          },
          launch_action: {
            type: "OPEN_URL",
            data: {
              args: "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/win-set-firefox-default-browser",
              where: "tabshifted",
            },
          },
        },
        {
          action: "dismiss",
          title: {
            string_id: "default-browser-guidance-notification-dismiss",
          },
          windowsSystemActivationType: true,
        },
      ],
      tag: "set-default-guidance-notification",
    },
    // Both Windows 10 and 11 return `os.windowsVersion == 10.0`. We limit to
    // only Windows 10 with `os.windowsBuildNumber < 22000`. We need this due to
    // Windows 10 and 11 having substantively different UX for Windows Settings.
    targeting:
      "os.isWindows && os.windowsVersion >= 10.0 && os.windowsBuildNumber < 22000",
    trigger: { id: "deeplinkedToWindowsSettingsUI" },
  },
  {
    id: "SET_DEFAULT_BROWSER_GUIDANCE_NOTIFICATION_WIN11",
    template: "toast_notification",
    content: {
      title: {
        string_id: "default-browser-guidance-notification-title",
      },
      body: {
        string_id:
          "default-browser-guidance-notification-body-instruction-win11",
      },
      launch_action: {
        type: "OPEN_URL",
        data: {
          args: "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/win-set-firefox-default-browser",
          where: "tabshifted",
        },
      },
      requireInteraction: true,
      actions: [
        {
          action: "info-page",
          title: {
            string_id: "default-browser-guidance-notification-info-page",
          },
          launch_action: {
            type: "OPEN_URL",
            data: {
              args: "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/win-set-firefox-default-browser",
              where: "tabshifted",
            },
          },
        },
        {
          action: "dismiss",
          title: {
            string_id: "default-browser-guidance-notification-dismiss",
          },
          windowsSystemActivationType: true,
        },
      ],
      tag: "set-default-guidance-notification",
    },
    // Both Windows 10 and 11 return `os.windowsVersion == 10.0`. We limit to
    // only Windows 11 with `os.windowsBuildNumber >= 22000`. We need this due to
    // Windows 10 and 11 having substantively different UX for Windows Settings.
    targeting:
      "os.isWindows && os.windowsVersion >= 10.0 && os.windowsBuildNumber >= 22000",
    trigger: { id: "deeplinkedToWindowsSettingsUI" },
  },
  {
    id: "FXA_ACCOUNTS_BADGE_REVISED",
    template: "toolbar_badge",
    content: {
      delay: 1000,
      target: "fxa-toolbar-menu-button",
    },
    skip_in_tests: "it's covered by browser_asrouter_toolbarbadge.js",
    targeting:
      "source == 'newtab' && !hasAccessedFxAPanel && !usesFirefoxSync && isFxAEnabled && !isFxASignedIn",
    trigger: {
      id: "defaultBrowserCheck",
    },
  },
  {
    id: "INFOBAR_DEFAULT_AND_PIN_87",
    groups: ["cfr"],
    content: {
      text: {
        string_id: "default-browser-notification-message",
      },
      type: "global",
      buttons: [
        {
          label: {
            string_id: "default-browser-notification-button",
          },
          action: {
            type: "PIN_AND_DEFAULT",
          },
          primary: true,
          accessKey: "P",
        },
      ],
      category: "cfrFeatures",
      bucket_id: "INFOBAR_DEFAULT_AND_PIN_87",
    },
    trigger: {
      id: "defaultBrowserCheck",
    },
    template: "infobar",
    frequency: {
      custom: [
        {
          cap: 1,
          period: 3024000000,
        },
      ],
      lifetime: 2,
    },
    targeting:
      "(firefoxVersion >= 138 && source == 'startup' && !isDefaultBrowser && !'browser.shell.checkDefaultBrowser'|preferenceValue && currentDate|date - 'browser.shell.userDisabledDefaultCheck'|preferenceValue * 1000 >= 604800000 && isMajorUpgrade != true && platformName != 'linux' && ((currentDate|date - profileAgeCreated) / 604800000) >= 5 && !activeNotifications && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features'|preferenceValue && ((currentDate|date - profileAgeCreated) / 604800000) < 15",
  },
  {
    id: "FINISH_SETUP_CHECKLIST",
    template: "feature_callout",
    content: {
      id: "FINISH_SETUP_CHECKLIST",
      template: "multistage",
      backdrop: "transparent",
      transitions: false,
      disableHistoryUpdates: true,
      screens: [
        {
          id: "FINISH_SETUP_CHECKLIST",
          anchors: [
            {
              selector: "#fxms-bmb-button",
              panel_position: {
                anchor_attachment: "bottomcenter",
                callout_attachment: "topright",
                offset_y: 4,
              },
              no_open_on_anchor: true,
            },
            {
              selector: "#FINISH_SETUP_BUTTON",
              panel_position: {
                anchor_attachment: "bottomcenter",
                callout_attachment: "topright",
                offset_y: 4,
              },
              no_open_on_anchor: true,
            },
          ],
          content: {
            page_event_listeners: [
              {
                params: {
                  type: "tourend",
                },
                action: {
                  type: "SET_PREF",
                  data: {
                    pref: {
                      name: "messaging-system-action.easyChecklist.open",
                      value: "false",
                    },
                  },
                },
              },
            ],
            position: "callout",
            title: {
              string_id: "onboarding-checklist-title",
              marginInline: "3px 40px",
              fontWeight: "600",
              fontSize: "16px",
            },
            title_logo: {
              alignment: "top",
              imageURL: "chrome://branding/content/about-logo.png",
            },
            action_checklist_subtitle: {
              string_id: "onboarding-checklist-subtitle",
            },
            tiles: {
              type: "action_checklist",
              data: [
                {
                  id: "action-checklist-set-to-default",
                  targeting: "isDefaultBrowserUncached",
                  label: {
                    string_id: "onboarding-checklist-set-default",
                  },
                  action: {
                    type: "SET_DEFAULT_BROWSER",
                  },
                },
                {
                  id: "action-checklist-pin-to-taskbar",
                  targeting: "!doesAppNeedPinUncached",
                  label: {
                    string_id: "onboarding-checklist-pin",
                  },
                  action: {
                    type: "MULTI_ACTION",
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
                {
                  id: "action-checklist-import-data",
                  targeting:
                    "hasMigratedBookmarks || hasMigratedCSVPasswords || hasMigratedHistory || hasMigratedPasswords",
                  label: {
                    string_id: "onboarding-checklist-import",
                  },
                  action: {
                    type: "SHOW_MIGRATION_WIZARD",
                  },
                  showExternalLinkIcon: true,
                },
                {
                  id: "action-checklist-explore-extensions",
                  targeting:
                    "'messaging-system-action.hasOpenedExtensions'|preferenceValue || addonsInfo.hasInstalledAddons",
                  label: {
                    string_id: "onboarding-checklist-extension",
                  },
                  action: {
                    type: "MULTI_ACTION",
                    data: {
                      actions: [
                        {
                          type: "SET_PREF",
                          data: {
                            pref: {
                              name: "messaging-system-action.hasOpenedExtensions",
                              value: "true",
                            },
                          },
                        },
                        {
                          type: "OPEN_URL",
                          data: {
                            args: "https://addons.mozilla.org/en-US/firefox/collections/4757633/b4d5649fb087446aa05add5f0258c3/?page=1&collection_sort=-popularity",
                            where: "current",
                          },
                        },
                      ],
                    },
                  },
                  showExternalLinkIcon: true,
                },
                {
                  id: "action-checklist-sign-in",
                  targeting: "isFxASignedIn",
                  label: {
                    string_id: "onboarding-checklist-sign-up",
                  },
                  action: {
                    type: "FXA_SIGNIN_FLOW",
                    data: {
                      entrypoint: "fx-onboarding-checklist",
                      extraParams: {
                        utm_content: "migration-onboarding",
                        utm_source: "fx-new-device-sync",
                        utm_medium: "firefox-desktop",
                        utm_campaign: "migration",
                      },
                    },
                  },
                  showExternalLinkIcon: true,
                },
              ],
            },
            dismiss_button: {
              action: {
                type: "MULTI_ACTION",
                dismiss: true,
                data: {
                  actions: [
                    {
                      type: "SET_PREF",
                      data: {
                        pref: {
                          name: "easyChecklist.open",
                          value: false,
                        },
                      },
                    },
                  ],
                },
              },
            },
          },
        },
      ],
    },
    priority: 3,
    targeting:
      "'messaging-system-action.easyChecklist.open' | preferenceValue == true",
    trigger: {
      id: "preferenceObserver",
      params: ["messaging-system-action.easyChecklist.open"],
    },
  },
  {
    id: "FINISH_SETUP_CHECKLIST",
    template: "feature_callout",
    content: {
      id: "FINISH_SETUP_CHECKLIST",
      template: "multistage",
      backdrop: "transparent",
      transitions: false,
      disableHistoryUpdates: true,
      screens: [
        {
          id: "FINISH_SETUP_CHECKLIST",
          anchors: [
            {
              selector: "#fxms-bmb-button",
              panel_position: {
                anchor_attachment: "bottomcenter",
                callout_attachment: "topright",
                offset_y: 4,
              },
              no_open_on_anchor: true,
            },
            {
              selector: "#FINISH_SETUP_BUTTON",
              panel_position: {
                anchor_attachment: "bottomcenter",
                callout_attachment: "topright",
                offset_y: 4,
              },
              no_open_on_anchor: true,
            },
            {
              selector: "#PersonalToolbar",
              panel_position: {
                anchor_attachment: "bottomright",
                callout_attachment: "topright",
                offset_x: -24,
                offset_y: 24,
              },
              no_open_on_anchor: true,
              hide_arrow: true,
            },
          ],
          content: {
            page_event_listeners: [
              {
                params: {
                  type: "tourend",
                },
                action: {
                  type: "SET_PREF",
                  data: {
                    pref: {
                      name: "messaging-system-action.easyChecklist.open",
                      value: "false",
                    },
                  },
                },
              },
            ],
            position: "callout",
            title: {
              string_id: "onboarding-checklist-title",
              marginInline: "3px 40px",
              fontWeight: "600",
              fontSize: "16px",
            },
            title_logo: {
              alignment: "top",
              imageURL: "chrome://branding/content/about-logo.png",
            },
            action_checklist_subtitle: {
              string_id: "onboarding-checklist-subtitle",
            },
            tiles: {
              type: "action_checklist",
              data: [
                {
                  id: "action-checklist-set-to-default",
                  targeting: "isDefaultBrowserUncached",
                  label: {
                    string_id: "onboarding-checklist-set-default",
                  },
                  action: {
                    type: "SET_DEFAULT_BROWSER",
                  },
                },
                {
                  id: "action-checklist-pin-to-taskbar",
                  targeting: "!doesAppNeedPinUncached",
                  label: {
                    string_id: "onboarding-checklist-pin",
                  },
                  action: {
                    type: "MULTI_ACTION",
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
                {
                  id: "action-checklist-import-data",
                  targeting:
                    "hasMigratedBookmarks || hasMigratedCSVPasswords || hasMigratedHistory || hasMigratedPasswords",
                  label: {
                    string_id: "onboarding-checklist-import",
                  },
                  action: {
                    type: "SHOW_MIGRATION_WIZARD",
                  },
                  showExternalLinkIcon: true,
                },
                {
                  id: "action-checklist-explore-extensions",
                  targeting:
                    "'messaging-system-action.hasOpenedExtensions'|preferenceValue || addonsInfo.hasInstalledAddons",
                  label: {
                    string_id: "onboarding-checklist-extension",
                  },
                  action: {
                    type: "MULTI_ACTION",
                    data: {
                      actions: [
                        {
                          type: "SET_PREF",
                          data: {
                            pref: {
                              name: "messaging-system-action.hasOpenedExtensions",
                              value: "true",
                            },
                          },
                        },
                        {
                          type: "OPEN_URL",
                          data: {
                            args: "https://addons.mozilla.org/en-US/firefox/collections/4757633/b4d5649fb087446aa05add5f0258c3/?page=1&collection_sort=-popularity",
                            where: "current",
                          },
                        },
                      ],
                    },
                  },
                  showExternalLinkIcon: true,
                },
                {
                  id: "action-checklist-sign-in",
                  targeting: "isFxASignedIn",
                  label: {
                    string_id: "onboarding-checklist-sign-up",
                  },
                  action: {
                    type: "FXA_SIGNIN_FLOW",
                    data: {
                      entrypoint: "fx-onboarding-checklist",
                      extraParams: {
                        utm_content: "migration-onboarding",
                        utm_source: "fx-new-device-sync",
                        utm_medium: "firefox-desktop",
                        utm_campaign: "migration",
                      },
                    },
                  },
                  showExternalLinkIcon: true,
                },
              ],
            },
            dismiss_button: {
              action: {
                type: "MULTI_ACTION",
                dismiss: true,
                data: {
                  actions: [
                    {
                      type: "SET_PREF",
                      data: {
                        pref: {
                          name: "easyChecklist.open",
                          value: false,
                        },
                      },
                    },
                  ],
                },
              },
            },
          },
        },
      ],
    },
    priority: 3,
    targeting:
      "'messaging-system-action.easyChecklist.open' | preferenceValue == true",
    trigger: {
      id: "messagesLoaded",
    },
  },
  {
    id: "MULTIPROFILE_DATA_COLLECTION_CHANGED_INFOBAR",
    template: "infobar",
    targeting: "true",
    content: {
      priority: 3,
      text: { string_id: "multiprofile-data-collection-message" },
      buttons: [
        {
          label: { string_id: "multiprofile-data-collection-view-settings" },
          action: {
            type: "OPEN_PREFERENCES_PAGE",
            data: { category: "privacy-reports" },
            dismiss: true,
          },
        },
        {
          label: { string_id: "multiprofile-data-collection-dismiss" },
          action: { type: "CANCEL", dismiss: true },
        },
      ],
    },
    trigger: {
      id: "selectableProfilesUpdated",
    },
  },
  {
    id: "updated-privacy-notice-notification-infobar",
    groups: ["cfr"],
    template: "infobar",
    content: {
      impression_action: {
        type: "MULTI_ACTION",
        orderedExecution: true,
        data: {
          actions: [
            {
              type: "SET_PREF",
              data: {
                pref: {
                  name: "termsofuse.firstAcceptedDate",
                  value: {
                    copyFromPref: "termsofuse.acceptedDate",
                  },
                },
              },
            },
            {
              type: "SET_PREF",
              data: {
                pref: {
                  name: "termsofuse.acceptedDate",
                  value: {
                    timestamp: true,
                  },
                },
              },
            },
          ],
        },
      },
      text: {
        string_id: "existing-user-privacy-notice-update-message",
        args: { where: "tabshifted" },
      },
      linkUrls: {
        "privacy-notice-link": "https://www.mozilla.org/privacy/firefox/next/",
      },
      type: "tab",
      buttons: [
        {
          label: {
            string_id: "existing-user-tou-learn-more",
          },
          action: {
            type: "OPEN_URL",
            data: {
              where: "tabshifted",
              args: "https://www.mozilla.org/privacy/firefox/update/",
            },
            dismiss: false,
          },
          accessKey: "L",
        },
      ],
    },
    trigger: {
      id: "defaultBrowserCheck",
    },
    frequency: {
      lifetime: 1,
    },
    priority: 2,
    targeting:
      "('termsofuse.acceptedDate'|preferenceValue != '0') && (('termsofuse.acceptedDate'|preferenceValue * 1) < 1765972800000)",
  },
  {
    id: "RELAY_50_MASKS_ANNOUNCEMENT",
    template: "feature_callout",
    groups: ["cfr"],
    trigger: {
      id: "defaultBrowserCheck",
    },
    targeting:
      "source == 'startup' && isFxASignedIn && isRelayFreeTier && relayEmailMasksCount > 1 && !activeNotifications && !isMajorUpgrade && userPrefs.cfrFeatures && (currentDate|date - profileAgeCreated|date) / 86400000 > 3",
    frequency: {
      lifetime: 1,
    },
    skip_in_tests: "We don't want it to pop up in tests",
    content: {
      id: "RELAY_50_MASKS_ANNOUNCEMENT",
      template: "multistage",
      backdrop: "transparent",
      transitions: false,
      disableHistoryUpdates: true,
      screens: [
        {
          id: "RELAY_SURVEY_SCREEN",
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
            width: "280px",
            logo: {
              imageURL:
                "chrome://browser/content/asrouter/assets/hero-relay-email-masks.svg",
              alt: "Firefox Relay email masks",
              height: "132px",
            },
            title: {
              string_id: "relay-50-masks-announcement-title",
            },
            subtitle: {
              string_id: "relay-50-masks-announcement-subtitle",
            },
            primary_button: {
              label: {
                string_id: "relay-50-masks-announcement-primary-button",
              },
              action: {
                type: "OPEN_URL",
                data: {
                  args: "https://relay.firefox.com",
                  where: "tabshifted",
                },
                dismiss: true,
              },
            },
            secondary_button: {
              label: {
                string_id: "relay-50-masks-announcement-secondary-button",
              },
              action: {
                dismiss: true,
              },
            },
          },
        },
      ],
    },
  },
];

const PREONBOARDING_MESSAGES = () => [
  {
    id: "NEW_USER_TOU_ONBOARDING",
    enabled: true,
    requireAction: true,
    currentVersion: 4,
    minimumVersion: 4,
    firstRunURL: "https://www.mozilla.org/privacy/firefox/",
    screens: [
      {
        id: "TOU_ONBOARDING_LOADING",
        targeting:
          "'browser.aboutwelcome.experimentsGate.enabled'|preferenceValue && (!'browser.aboutwelcome.experimentsGate.skipSplashIfLoaded'|preferenceValue || !experimentsLoaded)",
        advance_on_experiment_load: {
          minDisplayMs: 3000,
          maxDisplayMs: 10000,
        },
        force_hide_steps_indicator: true,
        content: {
          screen_style: {
            overflow: "auto",
            display: "block",
            padding: "0",
            width: "100vw",
            height: "100vh",
          },
          main_content_style: {
            display: "none",
          },
          logo: {
            imageURL:
              "chrome://activity-stream/content/data/content/assets/splash-logo.svg",
            height: "500px",
            width: "500px",
            style: {
              display: "flex",
              justifyContent: "center",
              alignItems: "center",
              flexGrow: "1",
            },
          },
        },
      },
      {
        id: "TOU_ONBOARDING",
        force_hide_steps_indicator: true,
        content: {
          action_buttons_above_content: true,
          screen_style: {
            overflow: "auto",
            display: "block",
            padding: "40px 0 0 0",
            width: "560px",
          },
          logo: {
            imageURL: "chrome://branding/content/about-logo.png",
            height: "40px",
            width: "40px",
          },
          title: {
            string_id: "preonboarding-title",
          },
          subtitle: {
            string_id: "preonboarding-subtitle",
            paddingInline: "24px",
          },
          tiles: [
            {
              type: "link",
              id: "terms_of_use",
              header: {
                title: {
                  string_id: "preonboarding-terms-of-use-header-button-title",
                },
              },
              action: {
                type: "OPEN_URL",
                data: {
                  args: "https://mozilla.org/about/legal/terms/firefox/?v=product",
                  where: "chromeless",
                },
              },
            },
            {
              type: "link",
              id: "privacy_notice",
              header: {
                title: {
                  string_id: "preonboarding-privacy-notice-header-button-title",
                },
              },
              action: {
                type: "OPEN_URL",
                data: {
                  args: "https://mozilla.org/privacy/firefox/?v=product",
                  where: "chromeless",
                },
              },
            },
            {
              type: "multiselect",
              header: {
                title: {
                  string_id: "preonboarding-manage-data-header-button-title",
                },
              },
              data: [
                {
                  id: "interaction-data",
                  type: "checkbox",
                  defaultValue: true,
                  label: {
                    string_id: "preonboarding-checklist-interaction-data-label",
                  },
                  description: {
                    string_id:
                      "preonboarding-checklist-interaction-data-description",
                  },
                  action: {
                    type: "SET_PREF",
                    data: {
                      pref: {
                        name: "datareporting.healthreport.uploadEnabled",
                        value: true,
                      },
                    },
                  },
                  uncheckedAction: {
                    type: "MULTI_ACTION",
                    data: {
                      orderedExecution: true,
                      actions: [
                        {
                          type: "SET_PREF",
                          data: {
                            pref: {
                              name: "datareporting.healthreport.uploadEnabled",
                              value: false,
                            },
                          },
                        },
                        {
                          type: "SUBMIT_ONBOARDING_OPT_OUT_PING",
                        },
                      ],
                    },
                  },
                },
                {
                  id: "crash-data",
                  type: "checkbox",
                  defaultValue: false,
                  label: {
                    string_id: "preonboarding-checklist-crash-reports-label",
                  },
                  description: {
                    string_id:
                      "preonboarding-checklist-crash-reports-description",
                  },
                  action: {
                    type: "SET_PREF",
                    data: {
                      pref: {
                        name: "browser.crashReports.unsubmittedCheck.autoSubmit2",
                        value: true,
                      },
                    },
                  },
                  uncheckedAction: {
                    type: "SET_PREF",
                    data: {
                      pref: {
                        name: "browser.crashReports.unsubmittedCheck.autoSubmit2",
                        value: false,
                      },
                    },
                  },
                },
              ],
            },
          ],
          primary_button: {
            label: {
              string_id: "preonboarding-primary-cta-v2",
              marginBlock: "30px 0",
              paddingBlock: "4px",
              paddingInline: "16px",
            },
            action: {
              type: "MULTI_ACTION",
              collectSelect: true,
              data: {
                orderedExecution: true,
                actions: [
                  {
                    type: "SET_TERMS_OF_USE_INTERACTED",
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
];

// Eventually, move Feature Callout messages to their own provider
const ONBOARDING_MESSAGES = () =>
  BASE_MESSAGES().concat(FeatureCalloutMessages.getMessages());

export const OnboardingMessageProvider = {
  async getExtraAttributes() {
    const [header, button_label] = await L10N.formatMessages([
      { id: "onboarding-welcome-header" },
      { id: "onboarding-start-browsing-button-label" },
    ]);
    return { header: header.value, button_label: button_label.value };
  },

  async getMessages() {
    const messages = await this.translateMessages(await ONBOARDING_MESSAGES());
    OnboardingMessageProvider.getRestoredFromBackupMessage(messages);
    return messages;
  },

  getPreonboardingMessages() {
    return PREONBOARDING_MESSAGES();
  },

  // If the user has restored from a backup, mutate the restore from backup message to appear once per backup by using the restoration timestamp as the unique message id
  getRestoredFromBackupMessage(messages) {
    const backupRestorationTimestamp = Services.prefs.getIntPref(
      "browser.backup.profile-restoration-date",
      0
    );

    if (backupRestorationTimestamp) {
      for (const msg of messages) {
        if (msg.id.startsWith("RESTORE_FROM_BACKUP")) {
          msg.id += `_${backupRestorationTimestamp}`;
          msg.content.id += `_${backupRestorationTimestamp}`;
        }
      }
    }
  },

  async getUntranslatedMessages() {
    // This is helpful for jsonSchema testing - since we are localizing in the provider
    const messages = await ONBOARDING_MESSAGES();
    return messages;
  },

  async translateMessages(messages) {
    let translatedMessages = [];
    for (const msg of messages) {
      let translatedMessage = { ...msg };

      // If the message has no content, do not attempt to translate it
      if (!translatedMessage.content) {
        translatedMessages.push(translatedMessage);
        continue;
      }

      // Translate any secondary buttons separately
      if (msg.content.secondary_button) {
        const [secondary_button_string] = await L10N.formatMessages([
          { id: msg.content.secondary_button.label.string_id },
        ]);
        translatedMessage.content.secondary_button.label =
          secondary_button_string.value;
      }
      if (msg.content.header) {
        const [header_string] = await L10N.formatMessages([
          { id: msg.content.header.string_id },
        ]);
        translatedMessage.content.header = header_string.value;
      }
      translatedMessages.push(translatedMessage);
    }
    return translatedMessages;
  },

  async _doesAppNeedPin(privateBrowsing = false) {
    const needPin = await lazy.ShellService.doesAppNeedPin(privateBrowsing);
    return needPin;
  },

  async _doesAppNeedDefault() {
    let checkDefault = Services.prefs.getBoolPref(
      "browser.shell.checkDefaultBrowser",
      false
    );
    let isDefault = await lazy.ShellService.isDefaultBrowser();
    return checkDefault && !isDefault;
  },

  _shouldShowPrivacySegmentationScreen() {
    return Services.prefs.getBoolPref(
      "browser.privacySegmentation.preferences.show"
    );
  },

  _doesHomepageNeedReset() {
    return (
      Services.prefs.prefHasUserValue(HOMEPAGE_PREF) ||
      Services.prefs.prefHasUserValue(NEWTAB_PREF)
    );
  },

  async getUpgradeMessage() {
    let message = (await OnboardingMessageProvider.getMessages()).find(
      ({ id }) => id === "FX_MR_106_UPGRADE"
    );

    let { content } = message;
    // Helper to find screens and remove them where applicable.
    function removeScreens(check) {
      const { screens } = content;
      for (let i = 0; i < screens?.length; i++) {
        if (check(screens[i])) {
          screens.splice(i--, 1);
        }
      }
    }

    // Helper to prepare mobile download screen content
    function prepareMobileDownload() {
      let mobileContent = content.screens.find(
        screen => screen.id === "UPGRADE_MOBILE_DOWNLOAD"
      )?.content;

      if (!mobileContent) {
        return;
      }
      if (!lazy.BrowserUtils.sendToDeviceEmailsSupported()) {
        // If send to device emails are not supported for a user's locale,
        // remove the send to device link and update the screen text
        delete mobileContent.cta_paragraph.action;
        mobileContent.cta_paragraph.text = {
          string_id: "mr2022-onboarding-no-mobile-download-cta-text",
        };
      }
      // Update CN specific QRCode url
      if (lazy.BrowserUtils.isChinaRepack()) {
        mobileContent.hero_image.url = `${mobileContent.hero_image.url.slice(
          0,
          mobileContent.hero_image.url.indexOf(".svg")
        )}-cn.svg`;
      }
    }

    let pinScreen = content.screens?.find(
      screen => screen.id === "UPGRADE_PIN_FIREFOX"
    );
    const needPin = await this._doesAppNeedPin();
    const needDefault = await this._doesAppNeedDefault();
    const needPrivatePin =
      !lazy.hidePrivatePin && (await this._doesAppNeedPin(true));
    const showSegmentation = this._shouldShowPrivacySegmentationScreen();

    //If a user has Firefox as default remove import screen
    if (!needDefault) {
      removeScreens(screen =>
        screen.id?.startsWith("UPGRADE_IMPORT_SETTINGS_EMBEDDED")
      );
    }

    // If already pinned, convert "pin" screen to "welcome" with desired action.
    let removeDefault = !needDefault;
    // If user doesn't need pin, update screen to set "default" or "get started" configuration
    if (!needPin && pinScreen) {
      // don't need to show the checkbox
      delete pinScreen.content.checkbox;

      removeDefault = true;
      let primary = pinScreen.content.primary_button;
      if (needDefault) {
        pinScreen.id = "UPGRADE_ONLY_DEFAULT";
        pinScreen.content.subtitle = {
          string_id: "mr2022-onboarding-existing-set-default-only-subtitle",
        };
        primary.label.string_id =
          "mr2022-onboarding-set-default-primary-button-label";

        // The "pin" screen will now handle "default" so remove other "default."
        primary.action.type = "SET_DEFAULT_BROWSER";
      } else {
        pinScreen.id = "UPGRADE_GET_STARTED";
        pinScreen.content.subtitle = {
          string_id: "mr2022-onboarding-get-started-primary-subtitle",
        };
        primary.label = {
          string_id: "mr2022-onboarding-get-started-primary-button-label",
        };
        delete primary.action.type;
      }
    }

    // If a user has Firefox private pinned remove pin private window screen
    // We also remove standalone pin private window screen if a user doesn't have
    // Firefox pinned in which case the option is shown as checkbox with UPGRADE_PIN_FIREFOX screen
    if (!needPrivatePin || needPin) {
      removeScreens(screen =>
        screen.id?.startsWith("UPGRADE_PIN_PRIVATE_WINDOW")
      );
    }

    if (!showSegmentation) {
      removeScreens(screen =>
        screen.id?.startsWith("UPGRADE_DATA_RECOMMENDATION")
      );
    }

    //If privatePin, remove checkbox from pinscreen
    if (!needPrivatePin) {
      delete content.screens?.find(
        screen => screen.id === "UPGRADE_PIN_FIREFOX"
      )?.content?.checkbox;
    }

    if (removeDefault) {
      removeScreens(screen => screen.id?.startsWith("UPGRADE_SET_DEFAULT"));
    }

    // Remove mobile download screen if user has sync enabled
    if (lazy.usesFirefoxSync && lazy.mobileDevices > 0) {
      removeScreens(screen => screen.id === "UPGRADE_MOBILE_DOWNLOAD");
    } else {
      prepareMobileDownload();
    }

    return message;
  },
};
