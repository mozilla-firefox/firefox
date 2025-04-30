/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Eventually, make this a messaging system
// provider instead of adding these message
// into OnboardingMessageProvider.sys.mjs
const PDFJS_PREF = "browser.pdfjs.feature-tour";
// Empty screens are included as placeholders to ensure step
// indicator shows the correct number of total steps in the tour
const ONE_DAY_IN_MS = 24 * 60 * 60 * 1000;

// Generate a JEXL targeting string based on the `complete` property being true
// in a given Feature Callout tour progress preference value (which is JSON).
const matchIncompleteTargeting = (prefName, defaultValue = false) => {
  // regExpMatch() is a JEXL filter expression. Here we check if 'complete'
  // exists in the pref's value, and returns true if the tour is incomplete.
  const prefVal = `'${prefName}' | preferenceValue`;
  // prefVal might be null if the preference doesn't exist. in this case, don't
  // try to pipe into regExpMatch.
  const completeMatch = `${prefVal} | regExpMatch('(?<=complete":)(.*)(?=})')`;
  return `((${prefVal}) ? ((${completeMatch}) ? (${completeMatch}[1] != "true") : ${String(
    defaultValue
  )}) : ${String(defaultValue)})`;
};

// Generate a JEXL targeting string based on the current screen id found in a
// given Feature Callout tour progress preference.
const matchCurrentScreenTargeting = (prefName, screenIdRegEx = ".*") => {
  // regExpMatch() is a JEXL filter expression. Here we check if 'screen' exists
  // in the pref's value, and if it matches the screenIdRegEx. Returns
  // null otherwise.
  const prefVal = `'${prefName}' | preferenceValue`;
  const screenMatch = `${prefVal} | regExpMatch('(?<=screen"\s*:)\s*"(${screenIdRegEx})(?="\s*,)')`;
  const screenValMatches = `(${screenMatch}) ? !!(${screenMatch}[1]) : false`;
  return `(${screenValMatches})`;
};

/**
 * add24HourImpressionJEXLTargeting -
 * Creates a "hasn't been viewed in > 24 hours"
 * JEXL string and adds it to each message specified
 *
 * @param {array} messageIds - IDs of messages that the targeting string will be added to
 * @param {string} prefix - The prefix of messageIDs that will used to create the JEXL string
 * @param {array} messages - The array of messages that will be edited
 * @returns {array} - The array of messages with the appropriate targeting strings edited
 */
function add24HourImpressionJEXLTargeting(
  messageIds,
  prefix,
  uneditedMessages
) {
  let noImpressionsIn24HoursString = uneditedMessages
    .filter(message => message.id.startsWith(prefix))
    .map(
      message =>
        // If the last impression is null or if epoch time
        // of the impression is < current time - 24hours worth of MS
        `(messageImpressions.${message.id}[messageImpressions.${
          message.id
        } | length - 1] == null || messageImpressions.${
          message.id
        }[messageImpressions.${message.id} | length - 1] < ${
          Date.now() - ONE_DAY_IN_MS
        })`
    )
    .join(" && ");

  // We're appending the string here instead of using
  // template strings to avoid a recursion error from
  // using the 'messages' variable within itself
  return uneditedMessages.map(message => {
    if (messageIds.includes(message.id)) {
      message.targeting += `&& ${noImpressionsIn24HoursString}`;
    }

    return message;
  });
}

// Exporting the about:firefoxview messages as a method here
// acts as a safety guard against mutations of the original objects
const MESSAGES = () => {
  let messages = [
    {
      id: "DESKTOP_TO_MOBILE_ADOPTION_SIGNED_INTO_ACCOUNT_NON_EU",
      template: "feature_callout",
      groups: ["cfr"],
      content: {
        id: "DESKTOP_TO_MOBILE_ADOPTION_SIGNED_INTO_ACCOUNT_NON_EU",
        padding: "16",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "DESKTOP_TO_MOBILE_ADOPTION_SIGNED_INTO_ACCOUNT_NON_EU",
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
                string_id: "desktop-to-mobile-headline",
                marginInline: "4px 0",
              },
              logo: {
                height: "128px",
                imageURL:
                  "chrome://browser/content/asrouter/assets/desktop-to-mobile-banner.svg",
              },
              subtitle: {
                string_id: "desktop-to-mobile-subtitle",
                marginBlock: "-44px 0",
                marginInline: "84px 0",
              },
              title_logo: {
                height: "103px",
                width: "75px",
                alignment: "top",
                marginBlock: "40px 0",
                marginInline: "32px",
                imageURL:
                  "chrome://browser/content/asrouter/assets/desktop-to-mobile-non-eu-QR.svg",
                alt: {
                  string_id: "desktop-to-mobile-qr-code-alt",
                },
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
              secondary_button: {
                action: {
                  type: "OPEN_ABOUT_PAGE",
                  data: {
                    args: "preferences?action=pair#sync",
                    where: "tab",
                  },
                  dismiss: true,
                },
                label: {
                  string_id: "sync-to-mobile-button-label",
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
            period: 2628000000,
          },
        ],
        lifetime: 3,
      },
      trigger: {
        id: "defaultBrowserCheck",
      },
      targeting:
        "(region in ['CA', 'US']) && isFxASignedIn && previousSessionEnd && !willShowDefaultPrompt && !activeNotifications && userPrefs.cfrFeatures && !(sync || {}).mobileDevices",
      skip_in_tests: "it's not tested in automation",
    },
    {
      id: "DESKTOP_TO_MOBILE_ADOPTION_SIGNED_INTO_ACCOUNT_EU",
      template: "feature_callout",
      groups: ["cfr"],
      content: {
        id: "DESKTOP_TO_MOBILE_ADOPTION_SIGNED_INTO_ACCOUNT_EU",
        padding: "16",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "DESKTOP_TO_MOBILE_ADOPTION_SIGNED_INTO_ACCOUNT_EU",
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
                string_id: "desktop-to-mobile-headline",
                marginInline: "4px 0",
              },
              logo: {
                height: "128px",
                imageURL:
                  "chrome://browser/content/asrouter/assets/desktop-to-mobile-banner.svg",
              },
              subtitle: {
                string_id: "desktop-to-mobile-subtitle",
                marginBlock: "-44px 0",
                marginInline: "84px 0",
              },
              title_logo: {
                height: "103px",
                width: "75px",
                alignment: "top",
                marginBlock: "40px 0",
                marginInline: "32px",
                imageURL:
                  "chrome://browser/content/asrouter/assets/desktop-to-mobile-eu-QR.svg",
                alt: {
                  string_id: "desktop-to-mobile-qr-code-alt",
                },
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
              secondary_button: {
                action: {
                  type: "OPEN_ABOUT_PAGE",
                  data: {
                    args: "preferences?action=pair#sync",
                    where: "tab",
                  },
                  dismiss: true,
                },
                label: {
                  string_id: "sync-to-mobile-button-label",
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
            period: 2628000000,
          },
        ],
        lifetime: 3,
      },
      trigger: {
        id: "defaultBrowserCheck",
      },
      targeting:
        "(locale in ['de', 'en-CA', 'en-GB', 'en-US', 'fr']) && (region in ['DE', 'FR', 'GB']) && isFxASignedIn && previousSessionEnd && !willShowDefaultPrompt && !activeNotifications && userPrefs.cfrFeatures && !(sync || {}).mobileDevices",
      skip_in_tests: "it's not tested in automation",
    },
    // Appears the first time a user uses the "save and close" action on a tab group,
    // anchored to the alltabs-button. Will only show if at least an hour has passed
    // since the CREATE_TAB_GROUP callout showed.
    {
      id: "SAVE_TAB_GROUP_ONBOARDING_CALLOUT",
      template: "feature_callout",
      groups: [],
      content: {
        id: "SAVE_TAB_GROUP_ONBOARDING_CALLOUT",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        screens: [
          {
            id: "SAVE_TAB_GROUP_ONBOARDING_CALLOUT_ALLTABS_BUTTON",
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
                string_id: "tab-groups-onboarding-saved-groups-title-2",
              },
              primary_button: {
                label: {
                  string_id: "tab-groups-onboarding-dismiss",
                },
                action: {
                  dismiss: true,
                },
              },
            },
          },
        ],
      },
      targeting:
        "('browser.tabs.groups.enabled' | preferenceValue) && userPrefs.cfrFeatures && (!messageImpressions.CREATE_TAB_GROUP_ONBOARDING_CALLOUT[messageImpressions.CREATE_TAB_GROUP_ONBOARDING_CALLOUT | length - 1] || messageImpressions.CREATE_TAB_GROUP_ONBOARDING_CALLOUT[messageImpressions.CREATE_TAB_GROUP_ONBOARDING_CALLOUT | length - 1] < currentDate|date - 3600000) && alltabsButtonAreaType != null",
      trigger: {
        id: "tabGroupSaved",
      },
      frequency: {
        lifetime: 1,
      },
      skip_in_tests: "it's not tested in automation",
    },
    // Appears the first time a user uses the "save and close" action on a tab group,
    // if the alltabs-button has been removed. Anchored to the urlbar. Will only show
    // if CREATE_TAB_GROUP callout has not shown, or at least an hour has passed since
    // the CREATE_TAB_GROUP callout showed.
    {
      id: "SAVE_TAB_GROUP_ONBOARDING_CALLOUT",
      template: "feature_callout",
      groups: [],
      content: {
        id: "SAVE_TAB_GROUP_ONBOARDING_CALLOUT",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        screens: [
          {
            id: "SAVE_TAB_GROUP_ONBOARDING_CALLOUT_URLBAR",
            anchors: [
              {
                selector: ".urlbar-input-box",
                panel_position: {
                  anchor_attachment: "bottomcenter",
                  callout_attachment: "topcenter",
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
                string_id:
                  "tab-groups-onboarding-saved-groups-no-alltabs-button-title-2",
              },
              primary_button: {
                label: {
                  string_id: "tab-groups-onboarding-dismiss",
                },
                action: {
                  dismiss: true,
                },
              },
            },
          },
        ],
      },
      targeting:
        "('browser.tabs.groups.enabled' | preferenceValue) && userPrefs.cfrFeatures && (!messageImpressions.CREATE_TAB_GROUP_ONBOARDING_CALLOUT[messageImpressions.CREATE_TAB_GROUP_ONBOARDING_CALLOUT | length - 1] || messageImpressions.CREATE_TAB_GROUP_ONBOARDING_CALLOUT[messageImpressions.CREATE_TAB_GROUP_ONBOARDING_CALLOUT | length - 1] < currentDate|date - 3600000) && alltabsButtonAreaType == null",
      trigger: {
        id: "tabGroupSaved",
      },
      frequency: {
        lifetime: 1,
      },
      skip_in_tests: "it's not tested in automation",
    },
    // Appears the first time a user creates a tab group, after clicking the "Done"
    // button. Anchored to the alltabs-button. Will only show if the SAVE_TAB_GROUP
    // callout has not shown, or if at least an hour has passed
    // since the SAVE_TAB_GROUP callout showed.
    {
      id: "CREATE_TAB_GROUP_ONBOARDING_CALLOUT",
      template: "feature_callout",
      groups: [],
      content: {
        id: "CREATE_TAB_GROUP_ONBOARDING_CALLOUT",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        screens: [
          {
            id: "CREATE_TAB_GROUP_ONBOARDING_CALLOUT_ALLTABS_BUTTON",
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
                string_id: "tab-groups-onboarding-create-group-title-2",
              },
              primary_button: {
                label: {
                  string_id: "tab-groups-onboarding-dismiss",
                },
                action: {
                  dismiss: true,
                },
              },
            },
          },
        ],
      },
      targeting:
        "('browser.tabs.groups.enabled' | preferenceValue) && userPrefs.cfrFeatures && (!messageImpressions.SAVE_TAB_GROUP_ONBOARDING_CALLOUT[messageImpressions.SAVE_TAB_GROUP_ONBOARDING_CALLOUT | length - 1] || messageImpressions.SAVE_TAB_GROUP_ONBOARDING_CALLOUT[messageImpressions.SAVE_TAB_GROUP_ONBOARDING_CALLOUT | length - 1] < currentDate|date - 3600000) && alltabsButtonAreaType != null",
      trigger: {
        id: "tabGroupCreated",
      },
      frequency: {
        lifetime: 1,
      },
      skip_in_tests: "it's not tested in automation",
    },
    // Appears the first time a user creates a tab group, after clicking the "Done"
    // button, if the alltabs-button has been removed. Anchored to the urlbar. Will
    // only show if the SAVE_TAB_GROUP callout has not shown, or if at least an hour
    // has passed since the SAVE_TAB_GROUP callout showed.
    {
      id: "CREATE_TAB_GROUP_ONBOARDING_CALLOUT",
      template: "feature_callout",
      groups: [],
      content: {
        id: "CREATE_TAB_GROUP_ONBOARDING_CALLOUT",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        screens: [
          {
            id: "CREATE_TAB_GROUP_ONBOARDING_CALLOUT_URLBAR",
            anchors: [
              {
                selector: ".urlbar-input-box",
                panel_position: {
                  anchor_attachment: "bottomcenter",
                  callout_attachment: "topcenter",
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
                string_id:
                  "tab-groups-onboarding-create-group-no-alltabs-button-title",
              },
              primary_button: {
                label: {
                  string_id: "tab-groups-onboarding-dismiss",
                },
                action: {
                  dismiss: true,
                },
              },
            },
          },
        ],
      },
      targeting:
        "('browser.tabs.groups.enabled' | preferenceValue) && userPrefs.cfrFeatures && (!messageImpressions.SAVE_TAB_GROUP_ONBOARDING_CALLOUT[messageImpressions.SAVE_TAB_GROUP_ONBOARDING_CALLOUT | length - 1] || messageImpressions.SAVE_TAB_GROUP_ONBOARDING_CALLOUT[messageImpressions.SAVE_TAB_GROUP_ONBOARDING_CALLOUT | length - 1] < currentDate|date - 3600000) && alltabsButtonAreaType == null",
      trigger: {
        id: "tabGroupCreated",
      },
      frequency: {
        lifetime: 1,
      },
      skip_in_tests: "it's not tested in automation",
    },
    // Appears after a browser restart if Session Restore is disabled, to direct
    // users to tab groups that were saved automatically. Anchored to the alltabs-button.
    {
      id: "SESSION_RESTORE_TAB_GROUP_CALLOUT",
      template: "feature_callout",
      groups: [],
      content: {
        id: "SESSION_RESTORE_TAB_GROUP_CALLOUT",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        screens: [
          {
            id: "SESSION_RESTORE_TAB_GROUP_CALLOUT_ALLTABS_BUTTON",
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
                string_id: "tab-groups-onboarding-session-restore-title",
              },
              primary_button: {
                label: {
                  string_id: "tab-groups-onboarding-dismiss",
                },
                action: {
                  dismiss: true,
                },
              },
            },
          },
        ],
      },
      targeting:
        "('browser.tabs.groups.enabled' | preferenceValue) && userPrefs.cfrFeatures && previousSessionEnd && ('browser.startup.page' | preferenceValue != 3) && savedTabGroups >= 1 && alltabsButtonAreaType != null",
      trigger: {
        id: "defaultBrowserCheck",
      },
      priority: 2,
      frequency: {
        lifetime: 1,
      },
      skip_in_tests: "not tested in automation",
    },
    // Appears after a browser restart if Session Restore is disabled, to direct
    // users to tab groups that were saved automatically, for users who have
    // removed the alltabs button. Anchored to the urlbar.
    {
      id: "SESSION_RESTORE_TAB_GROUP_CALLOUT",
      template: "feature_callout",
      groups: [],
      content: {
        id: "SESSION_RESTORE_TAB_GROUP_CALLOUT",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        screens: [
          {
            id: "SESSION_RESTORE_TAB_GROUP_CALLOUT_URLBAR",
            anchors: [
              {
                selector: ".urlbar-input-box",
                panel_position: {
                  anchor_attachment: "bottomcenter",
                  callout_attachment: "topcenter",
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
                string_id:
                  "tab-groups-onboarding-saved-groups-no-alltabs-button-title-2",
              },
              primary_button: {
                label: {
                  string_id: "tab-groups-onboarding-dismiss",
                },
                action: {
                  dismiss: true,
                },
              },
            },
          },
        ],
      },
      targeting:
        "('browser.tabs.groups.enabled' | preferenceValue) && userPrefs.cfrFeatures && previousSessionEnd && ('browser.startup.page' | preferenceValue != 3) && savedTabGroups >= 1 && alltabsButtonAreaType == null",
      trigger: {
        id: "defaultBrowserCheck",
      },
      priority: 2,
      frequency: {
        lifetime: 1,
      },
      skip_in_tests: "not tested in automation",
    },
    {
      id: "ADDONS_STAFF_PICK_PT_2",
      template: "feature_callout",
      groups: ["cfr"],
      content: {
        id: "ADDONS_STAFF_PICK_PT_2",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        screens: [
          {
            id: "ADDONS_STAFF_PICK_PT_2_A",
            anchors: [
              {
                selector: "#unified-extensions-button",
                panel_position: {
                  anchor_attachment: "bottomcenter",
                  callout_attachment: "topright",
                },
                arrow_width: "26.9",
              },
            ],
            content: {
              position: "callout",
              width: "310px",
              padding: 16,
              title_logo: {
                imageURL:
                  "chrome://browser/content/asrouter/assets/smiling-fox-icon.svg",
                width: "24px",
                height: "24px",
                marginInline: "4px 14px",
              },
              title: {
                raw: "Give your browsing a boost",
                marginInline: "0 48px",
              },
              subtitle: {
                raw: "Make browsing faster, safer, or just plain fun with Firefox add-ons. See what our staff recommends!",
                paddingInline: "34px 0",
              },
              primary_button: {
                label: {
                  raw: "Explore add-ons",
                },
                action: {
                  dismiss: true,
                  type: "OPEN_URL",
                  data: {
                    args: "https://addons.mozilla.org/en-US/firefox/collections/4757633/36d285535db74c6986abbeeed3e214/?page=1&collection_sort=added",
                    where: "tabshifted",
                  },
                },
              },
              dismiss_button: {
                action: {
                  dismiss: true,
                },
                size: "small",
                marginInline: "0 14px",
                marginBlock: "14px 0",
              },
            },
          },
        ],
      },
      targeting:
        "userPrefs.cfrAddons && userPrefs.cfrFeatures && localeLanguageCode == 'en' && ((currentDate|date - profileAgeCreated|date) / 86400000 < 28) && !screenImpressions.AW_AMO_INTRODUCE && !willShowDefaultPrompt && !activeNotifications && source == 'newtab' && previousSessionEnd",
      trigger: {
        id: "defaultBrowserCheck",
      },
      frequency: {
        lifetime: 1,
      },
    },
    {
      id: "FIREFOX_VIEW_TAB_PICKUP_REMINDER",
      template: "feature_callout",
      content: {
        id: "FIREFOX_VIEW_TAB_PICKUP_REMINDER",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "FIREFOX_VIEW_TAB_PICKUP_REMINDER",
            anchors: [
              {
                selector: "#tab-pickup-container",
                arrow_position: "top",
              },
            ],
            content: {
              position: "callout",
              title: {
                string_id:
                  "continuous-onboarding-firefox-view-tab-pickup-title",
              },
              subtitle: {
                string_id:
                  "continuous-onboarding-firefox-view-tab-pickup-subtitle",
              },
              logo: {
                imageURL: "chrome://browser/content/callout-tab-pickup.svg",
                darkModeImageURL:
                  "chrome://browser/content/callout-tab-pickup-dark.svg",
                height: "128px",
              },
              primary_button: {
                label: {
                  string_id: "mr1-onboarding-get-started-primary-button-label",
                },
                style: "secondary",
                action: {
                  type: "CLICK_ELEMENT",
                  navigate: true,
                  data: {
                    selector:
                      "#tab-pickup-container button.primary:not(#error-state-button)",
                  },
                },
              },
              dismiss_button: {
                action: {
                  navigate: true,
                },
              },
              page_event_listeners: [
                {
                  params: {
                    type: "toggle",
                    selectors: "#tab-pickup-container",
                  },
                  action: { reposition: true },
                },
              ],
            },
          },
        ],
      },
      priority: 2,
      targeting: `source == "about:firefoxview" && "browser.firefox-view.view-count" | preferenceValue > 2
    && (("identity.fxaccounts.enabled" | preferenceValue == false) || !(("services.sync.engine.tabs" | preferenceValue == true) && ("services.sync.username" | preferenceValue))) && (!messageImpressions.FIREFOX_VIEW_SPOTLIGHT[messageImpressions.FIREFOX_VIEW_SPOTLIGHT | length - 1] || messageImpressions.FIREFOX_VIEW_SPOTLIGHT[messageImpressions.FIREFOX_VIEW_SPOTLIGHT | length - 1] < currentDate|date - ${ONE_DAY_IN_MS})`,
      frequency: {
        lifetime: 1,
      },
      trigger: { id: "featureCalloutCheck" },
    },
    {
      id: "PDFJS_FEATURE_TOUR_A",
      template: "feature_callout",
      content: {
        id: "PDFJS_FEATURE_TOUR",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        tour_pref_name: PDFJS_PREF,
        screens: [
          {
            id: "FEATURE_CALLOUT_1_A",
            anchors: [
              {
                selector: "hbox#browser",
                arrow_position: "top-end",
                absolute_position: { top: "43px", right: "51px" },
              },
            ],
            content: {
              position: "callout",
              title: {
                string_id: "callout-pdfjs-edit-title",
              },
              subtitle: {
                string_id: "callout-pdfjs-edit-body-a",
              },
              primary_button: {
                label: {
                  string_id: "callout-pdfjs-edit-button",
                },
                style: "secondary",
                action: {
                  type: "SET_PREF",
                  data: {
                    pref: {
                      name: PDFJS_PREF,
                      value: JSON.stringify({
                        screen: "FEATURE_CALLOUT_2_A",
                        complete: false,
                      }),
                    },
                  },
                },
              },
              dismiss_button: {
                action: {
                  type: "SET_PREF",
                  data: {
                    pref: {
                      name: PDFJS_PREF,
                      value: JSON.stringify({
                        screen: "",
                        complete: true,
                      }),
                    },
                  },
                },
              },
            },
          },
          {
            id: "FEATURE_CALLOUT_2_A",
            anchors: [
              {
                selector: "hbox#browser",
                arrow_position: "top-end",
                absolute_position: { top: "43px", right: "23px" },
              },
            ],
            content: {
              position: "callout",
              title: {
                string_id: "callout-pdfjs-draw-title",
              },
              subtitle: {
                string_id: "callout-pdfjs-draw-body-a",
              },
              primary_button: {
                label: {
                  string_id: "callout-pdfjs-draw-button",
                },
                style: "secondary",
                action: {
                  type: "SET_PREF",
                  data: {
                    pref: {
                      name: PDFJS_PREF,
                      value: JSON.stringify({
                        screen: "",
                        complete: true,
                      }),
                    },
                  },
                },
              },
              dismiss_button: {
                action: {
                  type: "SET_PREF",
                  data: {
                    pref: {
                      name: PDFJS_PREF,
                      value: JSON.stringify({
                        screen: "",
                        complete: true,
                      }),
                    },
                  },
                },
              },
            },
          },
        ],
      },
      priority: 1,
      targeting: `source == "open" && ${matchCurrentScreenTargeting(
        PDFJS_PREF,
        "FEATURE_CALLOUT_[0-9]_A"
      )} && ${matchIncompleteTargeting(PDFJS_PREF)}`,
      trigger: { id: "pdfJsFeatureCalloutCheck" },
    },
    {
      id: "PDFJS_FEATURE_TOUR_B",
      template: "feature_callout",
      content: {
        id: "PDFJS_FEATURE_TOUR",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        tour_pref_name: PDFJS_PREF,
        screens: [
          {
            id: "FEATURE_CALLOUT_1_B",
            anchors: [
              {
                selector: "hbox#browser",
                arrow_position: "top-end",
                absolute_position: { top: "43px", right: "51px" },
              },
            ],
            content: {
              position: "callout",
              title: {
                string_id: "callout-pdfjs-edit-title",
              },
              subtitle: {
                string_id: "callout-pdfjs-edit-body-b",
              },
              primary_button: {
                label: {
                  string_id: "callout-pdfjs-edit-button",
                },
                style: "secondary",
                action: {
                  type: "SET_PREF",
                  data: {
                    pref: {
                      name: PDFJS_PREF,
                      value: JSON.stringify({
                        screen: "FEATURE_CALLOUT_2_B",
                        complete: false,
                      }),
                    },
                  },
                },
              },
              dismiss_button: {
                action: {
                  type: "SET_PREF",
                  data: {
                    pref: {
                      name: PDFJS_PREF,
                      value: JSON.stringify({
                        screen: "",
                        complete: true,
                      }),
                    },
                  },
                },
              },
            },
          },
          {
            id: "FEATURE_CALLOUT_2_B",
            anchors: [
              {
                selector: "hbox#browser",
                arrow_position: "top-end",
                absolute_position: { top: "43px", right: "23px" },
              },
            ],
            content: {
              position: "callout",
              title: {
                string_id: "callout-pdfjs-draw-title",
              },
              subtitle: {
                string_id: "callout-pdfjs-draw-body-b",
              },
              primary_button: {
                label: {
                  string_id: "callout-pdfjs-draw-button",
                },
                style: "secondary",
                action: {
                  type: "SET_PREF",
                  data: {
                    pref: {
                      name: PDFJS_PREF,
                      value: JSON.stringify({
                        screen: "",
                        complete: true,
                      }),
                    },
                  },
                },
              },
              dismiss_button: {
                action: {
                  type: "SET_PREF",
                  data: {
                    pref: {
                      name: PDFJS_PREF,
                      value: JSON.stringify({
                        screen: "",
                        complete: true,
                      }),
                    },
                  },
                },
              },
            },
          },
        ],
      },
      priority: 1,
      targeting: `source == "open" && ${matchCurrentScreenTargeting(
        PDFJS_PREF,
        "FEATURE_CALLOUT_[0-9]_B"
      )} && ${matchIncompleteTargeting(PDFJS_PREF)}`,
      trigger: { id: "pdfJsFeatureCalloutCheck" },
    },
    {
      // "Callout 1" in the Fakespot Figma spec
      id: "FAKESPOT_CALLOUT_CLOSED_OPTED_IN_DEFAULT",
      template: "feature_callout",
      content: {
        id: "FAKESPOT_CALLOUT_CLOSED_OPTED_IN_DEFAULT",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "FAKESPOT_CALLOUT_CLOSED_OPTED_IN_DEFAULT",
            anchors: [
              {
                selector: "#shopping-sidebar-button",
                panel_position: {
                  anchor_attachment: "bottomcenter",
                  callout_attachment: "topright",
                },
                no_open_on_anchor: true,
              },
            ],
            content: {
              position: "callout",
              title_logo: {
                imageURL:
                  "chrome://browser/content/shopping/assets/shopping.svg",
                alignment: "top",
              },
              title: {
                string_id: "shopping-callout-closed-opted-in-subtitle",
                marginInline: "3px 40px",
                fontWeight: "inherit",
              },
              dismiss_button: {
                action: { dismiss: true },
                size: "small",
                marginBlock: "24px 0",
                marginInline: "0 24px",
              },
              page_event_listeners: [
                {
                  params: {
                    type: "click",
                    selectors: "#shopping-sidebar-button",
                  },
                  action: { dismiss: true },
                },
              ],
            },
          },
        ],
      },
      priority: 1,
      // Auto-open feature flag is not enabled; User is opted in; First time closing sidebar; Has not seen either on-closed callout before; Has not opted out of CFRs.
      targeting: `isSidebarClosing && 'browser.shopping.experience2023.autoOpen.enabled' | preferenceValue != true && 'browser.shopping.experience2023.optedIn' | preferenceValue == 1 && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue != false && !messageImpressions.FAKESPOT_CALLOUT_CLOSED_OPTED_IN_DEFAULT|length && !messageImpressions.FAKESPOT_CALLOUT_CLOSED_NOT_OPTED_IN_DEFAULT|length`,
      trigger: { id: "shoppingProductPageWithSidebarClosed" },
      frequency: { lifetime: 1 },
    },
    {
      // "Callout 3" in the Fakespot Figma spec
      id: "FAKESPOT_CALLOUT_CLOSED_NOT_OPTED_IN_DEFAULT",
      template: "feature_callout",
      content: {
        id: "FAKESPOT_CALLOUT_CLOSED_NOT_OPTED_IN_DEFAULT",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "FAKESPOT_CALLOUT_CLOSED_NOT_OPTED_IN_DEFAULT",
            anchors: [
              {
                selector: "#shopping-sidebar-button",
                panel_position: {
                  anchor_attachment: "bottomcenter",
                  callout_attachment: "topright",
                },
                no_open_on_anchor: true,
              },
            ],
            content: {
              position: "callout",
              title_logo: {
                imageURL:
                  "chrome://browser/content/shopping/assets/shopping.svg",
              },
              title: {
                string_id: "shopping-callout-closed-not-opted-in-title",
                marginInline: "3px 40px",
              },
              subtitle: {
                string_id: "shopping-callout-closed-not-opted-in-subtitle",
              },
              dismiss_button: {
                action: { dismiss: true },
                size: "small",
                marginBlock: "24px 0",
                marginInline: "0 24px",
              },
              page_event_listeners: [
                {
                  params: {
                    type: "click",
                    selectors: "#shopping-sidebar-button",
                  },
                  action: { dismiss: true },
                },
              ],
            },
          },
        ],
      },
      priority: 1,
      // Auto-open feature flag is not enabled; User is not opted in; First time closing sidebar; Has not seen either on-closed callout before; Has not opted out of CFRs.
      targeting: `isSidebarClosing && 'browser.shopping.experience2023.autoOpen.enabled' | preferenceValue != true && 'browser.shopping.experience2023.optedIn' | preferenceValue != 1 && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue != false && !messageImpressions.FAKESPOT_CALLOUT_CLOSED_OPTED_IN_DEFAULT|length && !messageImpressions.FAKESPOT_CALLOUT_CLOSED_NOT_OPTED_IN_DEFAULT|length`,
      trigger: { id: "shoppingProductPageWithSidebarClosed" },
      frequency: { lifetime: 1 },
    },
    {
      // "callout 2" in the Fakespot Figma spec
      id: "FAKESPOT_CALLOUT_PDP_OPTED_IN_DEFAULT",
      template: "feature_callout",
      content: {
        id: "FAKESPOT_CALLOUT_PDP_OPTED_IN_DEFAULT",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "FAKESPOT_CALLOUT_PDP_OPTED_IN_DEFAULT",
            anchors: [
              {
                selector: "#shopping-sidebar-button",
                panel_position: {
                  anchor_attachment: "bottomcenter",
                  callout_attachment: "topright",
                },
                no_open_on_anchor: true,
              },
            ],
            content: {
              position: "callout",
              title: { string_id: "shopping-callout-pdp-opted-in-title" },
              subtitle: { string_id: "shopping-callout-pdp-opted-in-subtitle" },
              logo: {
                imageURL:
                  "chrome://browser/content/shopping/assets/ratingLight.avif",
                darkModeImageURL:
                  "chrome://browser/content/shopping/assets/ratingDark.avif",
                height: "216px",
              },
              dismiss_button: {
                action: { dismiss: true },
                size: "small",
                marginBlock: "24px 0",
                marginInline: "0 24px",
              },
              page_event_listeners: [
                {
                  params: {
                    type: "click",
                    selectors: "#shopping-sidebar-button",
                  },
                  action: { dismiss: true },
                },
              ],
            },
          },
        ],
      },
      priority: 1,
      // Auto-open feature flag is not enabled; User is opted in; Has not opted out of CFRs; Has seen either on-closed callout before, but not within the last 24hrs or in this session.
      targeting: `!isSidebarClosing && 'browser.shopping.experience2023.autoOpen.enabled' | preferenceValue != true && 'browser.shopping.experience2023.optedIn' | preferenceValue == 1 && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue != false && ((currentDate | date - messageImpressions.FAKESPOT_CALLOUT_CLOSED_OPTED_IN_DEFAULT[messageImpressions.FAKESPOT_CALLOUT_CLOSED_OPTED_IN_DEFAULT | length - 1] | date) / 3600000 > 24 || (currentDate | date - messageImpressions.FAKESPOT_CALLOUT_CLOSED_NOT_OPTED_IN_DEFAULT[messageImpressions.FAKESPOT_CALLOUT_CLOSED_NOT_OPTED_IN_DEFAULT | length - 1] | date) / 3600000 > 24)`,
      trigger: { id: "shoppingProductPageWithSidebarClosed" },
      frequency: { lifetime: 1 },
    },
    {
      // "Callout 1" in the Fakespot Figma spec, but
      // targeting not opted-in users only for rediscoverability experiment 2.
      id: "FAKESPOT_CALLOUT_CLOSED_NOT_OPTED_IN_AUTO_OPEN",
      template: "feature_callout",
      content: {
        id: "FAKESPOT_CALLOUT_CLOSED_NOT_OPTED_IN_AUTO_OPEN",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "FAKESPOT_CALLOUT_CLOSED_NOT_OPTED_IN_AUTO_OPEN",
            anchors: [
              {
                selector: "#shopping-sidebar-button",
                panel_position: {
                  anchor_attachment: "bottomcenter",
                  callout_attachment: "topright",
                },
                no_open_on_anchor: true,
              },
            ],
            content: {
              position: "callout",
              width: "401px",
              title: {
                string_id: "shopping-callout-closed-not-opted-in-revised-title",
              },
              subtitle: {
                string_id:
                  "shopping-callout-closed-not-opted-in-revised-subtitle",
                letterSpacing: "0",
              },
              logo: {
                imageURL:
                  "chrome://browser/content/shopping/assets/priceTagButtonCallout.svg",
                height: "214px",
              },
              dismiss_button: {
                action: { dismiss: true },
                size: "small",
                marginBlock: "28px 0",
                marginInline: "0 28px",
              },
              primary_button: {
                label: {
                  string_id:
                    "shopping-callout-closed-not-opted-in-revised-button",
                  marginBlock: "0 -8px",
                },
                style: "secondary",
                action: {
                  dismiss: true,
                },
              },
              page_event_listeners: [
                {
                  params: {
                    type: "click",
                    selectors: "#shopping-sidebar-button",
                  },
                  action: { dismiss: true },
                },
              ],
            },
          },
        ],
      },
      priority: 1,
      // Auto-open feature flag is enabled; User is not opted in; First time closing sidebar; Has not opted out of CFRs.
      targeting: `isSidebarClosing && 'browser.shopping.experience2023.autoOpen.enabled' | preferenceValue == true && 'browser.shopping.experience2023.optedIn' | preferenceValue != 1 && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue != false`,
      trigger: { id: "shoppingProductPageWithSidebarClosed" },
      frequency: { lifetime: 1 },
      skip_in_tests:
        "it's not tested in automation and might pop up unexpectedly during review checker tests",
    },
    {
      // "Callout 3" in the Fakespot Figma spec, but
      // displayed if auto-open version of "callout 1" was seen already and 24 hours have passed.
      id: "FAKESPOT_CALLOUT_PDP_NOT_OPTED_IN_REMINDER",
      template: "feature_callout",
      content: {
        id: "FAKESPOT_CALLOUT_PDP_NOT_OPTED_IN_REMINDER",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "FAKESPOT_CALLOUT_PDP_NOT_OPTED_IN_REMINDER",
            anchors: [
              {
                selector: "#shopping-sidebar-button",
                panel_position: {
                  anchor_attachment: "bottomcenter",
                  callout_attachment: "topright",
                },
                no_open_on_anchor: true,
              },
            ],
            content: {
              position: "callout",
              width: "401px",
              title: {
                string_id: "shopping-callout-not-opted-in-reminder-title",
                fontSize: "20px",
                letterSpacing: "0",
              },
              subtitle: {
                string_id: "shopping-callout-not-opted-in-reminder-subtitle",
                letterSpacing: "0",
              },
              logo: {
                imageURL:
                  "chrome://browser/content/shopping/assets/reviewsVisualCallout.svg",
                alt: {
                  string_id: "shopping-callout-not-opted-in-reminder-img-alt",
                },
                height: "214px",
              },
              dismiss_button: {
                action: {
                  type: "MULTI_ACTION",
                  collectSelect: true,
                  data: {
                    actions: [],
                  },
                  dismiss: true,
                },
                size: "small",
                marginBlock: "28px 0",
                marginInline: "0 28px",
              },
              primary_button: {
                label: {
                  string_id:
                    "shopping-callout-not-opted-in-reminder-close-button",
                  marginBlock: "0 -8px",
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
              secondary_button: {
                label: {
                  string_id:
                    "shopping-callout-not-opted-in-reminder-open-button",
                  marginBlock: "0 -8px",
                },
                style: "primary",
                action: {
                  type: "MULTI_ACTION",
                  collectSelect: true,
                  data: {
                    actions: [
                      {
                        type: "SET_PREF",
                        data: {
                          pref: {
                            name: "browser.shopping.experience2023.active",
                            value: true,
                          },
                        },
                      },
                    ],
                  },
                  dismiss: true,
                },
              },
              page_event_listeners: [
                {
                  params: {
                    type: "click",
                    selectors: "#shopping-sidebar-button",
                  },
                  action: { dismiss: true },
                },
              ],
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
                      string_id:
                        "shopping-callout-not-opted-in-reminder-ignore-checkbox",
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
                          name: "messaging-system-action.shopping-callouts-1-block",
                          value: true,
                        },
                      },
                    },
                  },
                ],
              },
            },
          },
        ],
      },
      priority: 2,
      // Auto-open feature flag is enabled; User is not opted in; Has not opted out of CFRs; Has seen callout 1 before, but not within the last 5 days.
      targeting:
        "!isSidebarClosing && 'browser.shopping.experience2023.autoOpen.enabled' | preferenceValue == true && 'browser.shopping.experience2023.optedIn' | preferenceValue == 0 && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue != false && !'messaging-system-action.shopping-callouts-1-block' | preferenceValue && (currentDate | date - messageImpressions.FAKESPOT_CALLOUT_CLOSED_NOT_OPTED_IN_AUTO_OPEN[messageImpressions.FAKESPOT_CALLOUT_CLOSED_NOT_OPTED_IN_AUTO_OPEN | length - 1] | date) / 3600000 > 24",
      trigger: {
        id: "shoppingProductPageWithSidebarClosed",
      },
      frequency: {
        custom: [
          {
            cap: 1,
            period: 432000000,
          },
        ],
        lifetime: 3,
      },
      skip_in_tests:
        "it's not tested in automation and might pop up unexpectedly during review checker tests",
    },
    {
      // "Callout 3" in the Review Checker Integrated Sidebar Migration Figma spec
      // For non-opted in users
      // Triggered if the Review Checker is panel is closed and user visits a product page
      // Explains why you should use Review Checker and prompts to opt in
      // Horizontal tabs
      id: "REVIEW_CHECKER_CALLOUT_PDP_NOT_OPTED_IN_REMINDER",
      template: "feature_callout",
      content: {
        id: "REVIEW_CHECKER_CALLOUT_PDP_NOT_OPTED_IN_REMINDER",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "REVIEW_CHECKER_CALLOUT_PDP_NOT_OPTED_IN_REMINDER_HORIZONTAL",
            anchors: [
              {
                selector:
                  "#sidebar-main:not([sidebar-positionend]) > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar']",
                panel_position: {
                  anchor_attachment: "rightcenter",
                  callout_attachment: "topleft",
                },
                no_open_on_anchor: true,
              },
              {
                selector:
                  "#sidebar-main[sidebar-positionend] > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar']",
                panel_position: {
                  anchor_attachment: "leftcenter",
                  callout_attachment: "topright",
                },
                no_open_on_anchor: true,
              },
            ],
            content: {
              position: "callout",
              width: "401px",
              title: {
                string_id: "shopping-callout-opt-in-integrated-headline",
                fontSize: "20px",
                letterSpacing: "0",
              },
              subtitle: {
                string_id:
                  "shopping-callout-not-opted-in-integrated-paragraph1",
                letterSpacing: "0",
              },
              above_button_content: [
                {
                  type: "text",
                  text: {
                    string_id:
                      "shopping-callout-not-opted-in-integrated-paragraph2",
                    letterSpacing: "0",
                    textAlign: "start",
                    fontSize: "0.831em",
                    marginBlock: "0",
                    marginInline: "0",
                  },
                  link_keys: ["privacy_policy", "terms_of_use"],
                  font_styles: "legal",
                },
              ],
              privacy_policy: {
                action: {
                  type: "OPEN_URL",
                  data: {
                    args: "https://www.mozilla.org/privacy/firefox?utm_source=review-checker&utm_campaign=privacy-policy&utm_medium=in-product&utm_term=opt-in-screen",
                    where: "tab",
                  },
                },
              },
              terms_of_use: {
                action: {
                  type: "OPEN_URL",
                  data: {
                    args: "https://www.fakespot.com/terms?utm_source=review-checker&utm_campaign=terms-of-use&utm_medium=in-product",
                    where: "tab",
                  },
                },
              },
              logo: {
                imageURL:
                  "chrome://browser/content/shopping/assets/reviewsVisualCallout.svg",
                alt: {
                  string_id: "shopping-callout-not-opted-in-reminder-img-alt",
                },
                height: "214px",
              },
              dismiss_button: {
                action: { dismiss: true },
                size: "small",
                marginBlock: "20px 0",
                marginInline: "0 28px",
                background: true,
              },
              secondary_button: {
                label: {
                  string_id:
                    "shopping-callout-not-opted-in-integrated-reminder-accept-button",
                  marginBlock: "0 -8px",
                },
                style: "primary",
                action: {
                  type: "MULTI_ACTION",
                  collectSelect: true,
                  data: {
                    actions: [
                      {
                        type: "SET_PREF",
                        data: {
                          pref: {
                            name: "browser.shopping.experience2023.optedIn",
                            value: 1,
                          },
                        },
                      },
                      {
                        type: "OPEN_SIDEBAR",
                        data: "viewReviewCheckerSidebar",
                      },
                    ],
                  },
                  dismiss: true,
                },
              },
              additional_button: {
                label: {
                  string_id:
                    "shopping-callout-not-opted-in-integrated-reminder-dismiss-button",
                  marginBlock: "0 -8px",
                },
                style: "secondary",
                action: { dismiss: true },
              },
              submenu_button: {
                submenu: [
                  {
                    type: "action",
                    label: {
                      string_id: "split-dismiss-button-dont-show-option",
                    },
                    action: {
                      type: "SET_PREF",
                      data: {
                        pref: {
                          name: "messaging-system-action.shopping-block-review-checker-callout-3",
                          value: true,
                        },
                      },
                      dismiss: true,
                    },
                    id: "split-dismiss-button-dont-show-option",
                  },
                  {
                    type: "action",
                    label: {
                      string_id: "split-dismiss-button-show-fewer-option",
                    },
                    action: {
                      type: "MULTI_ACTION",
                      collectSelect: true,
                      data: {
                        actions: [
                          {
                            type: "SET_PREF",
                            data: {
                              pref: {
                                name: "messaging-system-action.shopping-block-review-checker-callouts",
                                value: true,
                              },
                            },
                          },
                          {
                            type: "SET_PREF",
                            data: {
                              pref: {
                                name: "messaging-system-action.shopping-block-review-checker-callout-3",
                                value: true,
                              },
                            },
                          },
                        ],
                      },
                      dismiss: true,
                    },
                    id: "split-dismiss-button-show-fewer-option",
                  },
                  {
                    type: "separator",
                  },
                  {
                    type: "action",
                    label: {
                      string_id: "split-dismiss-button-manage-settings-option",
                    },
                    action: {
                      type: "OPEN_ABOUT_PAGE",
                      data: {
                        args: "settings#general-cfrfeatures",
                        where: "tab",
                      },
                      dismiss: true,
                    },
                    id: "split-dismiss-button-manage-settings-option",
                  },
                ],
                attached_to: "additional_button",
                style: "secondary",
                label: {
                  marginBlock: "0 -8px",
                  string_id: "split-dismiss-button-default-label",
                },
              },
              tiles: {
                type: "multiselect",
                style: {
                  flexDirection: "column",
                  alignItems: "flex-start",
                },
                data: [],
              },
            },
          },
        ],
      },
      priority: 2,
      // Review checker is added to the sidebar; Sidebar is closed; Review checker callouts have not been disabled; Integrated Sidebar is enabled; User is not opted in; Has not opted out of CFRs; Onboarding impression was at least 24 hr ago; Frequency of 5 days;
      targeting:
        "'sidebar.main.tools' | preferenceValue | regExpMatch('reviewchecker') && !'messaging-system-action.shopping-block-review-checker-callout-3' | preferenceValue && !'messaging-system-action.shopping-block-review-checker-callouts' | preferenceValue && isReviewCheckerInSidebarClosed && 'browser.shopping.experience2023.integratedSidebar' | preferenceValue && 'sidebar.revamp' | preferenceValue && 'browser.shopping.experience2023.optedIn' | preferenceValue == 0 && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue != false && 'browser.shopping.experience2023.firstImpressionTime' | preferenceValue && ((currentDate | date - ('browser.shopping.experience2023.firstImpressionTime' | preferenceValue * 1000)) / 3600000) > 24 && !'sidebar.verticalTabs' | preferenceValue",
      trigger: {
        id: "shoppingProductPageWithIntegratedRCSidebarClosed",
      },
      frequency: {
        custom: [
          {
            cap: 1,
            period: 432000000,
          },
        ],
        lifetime: 3,
      },
      skip_in_tests:
        "it's not tested in automation and might pop up unexpectedly during review checker tests",
    },
    {
      // "Callout 3" in the Review Checker Integrated Sidebar Migration Figma spec
      // For non-opted in users
      // Triggered if the Review Checker is panel is closed and user visits a product page
      // Explains why you should use Review Checker and prompts to opt in
      // Vertical tabs
      id: "REVIEW_CHECKER_CALLOUT_PDP_NOT_OPTED_IN_REMINDER",
      template: "feature_callout",
      content: {
        id: "REVIEW_CHECKER_CALLOUT_PDP_NOT_OPTED_IN_REMINDER",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "REVIEW_CHECKER_CALLOUT_PDP_NOT_OPTED_IN_REMINDER_VERTICAL",
            anchors: [
              {
                selector:
                  "#sidebar-main:not([sidebar-positionend]) > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar'].tools-overflow",
                panel_position: {
                  anchor_attachment: "topcenter",
                  callout_attachment: "bottomleft",
                },
                no_open_on_anchor: true,
              },
              {
                selector:
                  "#sidebar-main[sidebar-positionend] > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar'].tools-overflow",
                panel_position: {
                  anchor_attachment: "topcenter",
                  callout_attachment: "bottomright",
                },
                no_open_on_anchor: true,
              },
              {
                selector:
                  "#sidebar-main:not([sidebar-positionend]) > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar']",
                panel_position: {
                  anchor_attachment: "rightcenter",
                  callout_attachment: "bottomleft",
                },
                no_open_on_anchor: true,
              },
              {
                selector:
                  "#sidebar-main[sidebar-positionend] > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar']",
                panel_position: {
                  anchor_attachment: "leftcenter",
                  callout_attachment: "bottomright",
                },
                no_open_on_anchor: true,
              },
            ],
            content: {
              position: "callout",
              width: "401px",
              title: {
                string_id: "shopping-callout-opt-in-integrated-headline",
                fontSize: "20px",
                letterSpacing: "0",
              },
              subtitle: {
                string_id:
                  "shopping-callout-not-opted-in-integrated-paragraph1",
                letterSpacing: "0",
              },
              above_button_content: [
                {
                  type: "text",
                  text: {
                    string_id:
                      "shopping-callout-not-opted-in-integrated-paragraph2",
                    letterSpacing: "0",
                    textAlign: "start",
                    fontSize: "0.831em",
                    marginBlock: "0",
                    marginInline: "0",
                  },
                  link_keys: ["privacy_policy", "terms_of_use"],
                  font_styles: "legal",
                },
              ],
              privacy_policy: {
                action: {
                  type: "OPEN_URL",
                  data: {
                    args: "https://www.mozilla.org/privacy/firefox?utm_source=review-checker&utm_campaign=privacy-policy&utm_medium=in-product&utm_term=opt-in-screen",
                    where: "tab",
                  },
                },
              },
              terms_of_use: {
                action: {
                  type: "OPEN_URL",
                  data: {
                    args: "https://www.fakespot.com/terms?utm_source=review-checker&utm_campaign=terms-of-use&utm_medium=in-product",
                    where: "tab",
                  },
                },
              },
              logo: {
                imageURL:
                  "chrome://browser/content/shopping/assets/reviewsVisualCallout.svg",
                alt: {
                  string_id: "shopping-callout-not-opted-in-reminder-img-alt",
                },
                height: "214px",
              },
              dismiss_button: {
                action: { dismiss: true },
                size: "small",
                marginBlock: "20px 0",
                marginInline: "0 28px",
                background: true,
              },
              secondary_button: {
                label: {
                  string_id:
                    "shopping-callout-not-opted-in-integrated-reminder-accept-button",
                  marginBlock: "0 -8px",
                },
                style: "primary",
                action: {
                  type: "MULTI_ACTION",
                  collectSelect: true,
                  data: {
                    actions: [
                      {
                        type: "SET_PREF",
                        data: {
                          pref: {
                            name: "browser.shopping.experience2023.optedIn",
                            value: 1,
                          },
                        },
                      },
                      {
                        type: "OPEN_SIDEBAR",
                        data: "viewReviewCheckerSidebar",
                      },
                    ],
                  },
                  dismiss: true,
                },
              },
              additional_button: {
                label: {
                  string_id:
                    "shopping-callout-not-opted-in-integrated-reminder-dismiss-button",
                  marginBlock: "0 -8px",
                },
                style: "secondary",
                action: { dismiss: true },
              },
              submenu_button: {
                submenu: [
                  {
                    type: "action",
                    label: {
                      string_id: "split-dismiss-button-dont-show-option",
                    },
                    action: {
                      type: "SET_PREF",
                      data: {
                        pref: {
                          name: "messaging-system-action.shopping-block-review-checker-callout-3",
                          value: true,
                        },
                      },
                      dismiss: true,
                    },
                    id: "split-dismiss-button-dont-show-option",
                  },
                  {
                    type: "action",
                    label: {
                      string_id: "split-dismiss-button-show-fewer-option",
                    },
                    action: {
                      type: "MULTI_ACTION",
                      collectSelect: true,
                      data: {
                        actions: [
                          {
                            type: "SET_PREF",
                            data: {
                              pref: {
                                name: "messaging-system-action.shopping-block-review-checker-callouts",
                                value: true,
                              },
                            },
                          },
                          {
                            type: "SET_PREF",
                            data: {
                              pref: {
                                name: "messaging-system-action.shopping-block-review-checker-callout-3",
                                value: true,
                              },
                            },
                          },
                        ],
                      },
                      dismiss: true,
                    },
                    id: "split-dismiss-button-show-fewer-option",
                  },
                  {
                    type: "separator",
                  },
                  {
                    type: "action",
                    label: {
                      string_id: "split-dismiss-button-manage-settings-option",
                    },
                    action: {
                      type: "OPEN_ABOUT_PAGE",
                      data: {
                        args: "settings#general-cfrfeatures",
                        where: "tab",
                      },
                      dismiss: true,
                    },
                    id: "split-dismiss-button-manage-settings-option",
                  },
                ],
                attached_to: "additional_button",
                style: "secondary",
                label: {
                  marginBlock: "0 -8px",
                  string_id: "split-dismiss-button-default-label",
                },
              },
              tiles: {
                type: "multiselect",
                style: {
                  flexDirection: "column",
                  alignItems: "flex-start",
                },
                data: [],
              },
            },
          },
        ],
      },
      priority: 2,
      // Review checker is added to the sidebar; Sidebar is closed; Review checker callouts have not been disabled; Integrated Sidebar is enabled; User is not opted in; Has not opted out of CFRs; Onboarding impression was at least 24 hr ago; Frequency of 5 days;
      targeting:
        "'sidebar.main.tools' | preferenceValue | regExpMatch('reviewchecker') && !'messaging-system-action.shopping-block-review-checker-callout-3' | preferenceValue && !'messaging-system-action.shopping-block-review-checker-callouts' | preferenceValue && isReviewCheckerInSidebarClosed && 'browser.shopping.experience2023.integratedSidebar' | preferenceValue && 'sidebar.revamp' | preferenceValue && 'browser.shopping.experience2023.optedIn' | preferenceValue == 0 && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue != false && 'browser.shopping.experience2023.firstImpressionTime' | preferenceValue && ((currentDate | date - ('browser.shopping.experience2023.firstImpressionTime' | preferenceValue * 1000)) / 3600000) > 24 && 'sidebar.verticalTabs' | preferenceValue",
      trigger: {
        id: "shoppingProductPageWithIntegratedRCSidebarClosed",
      },
      frequency: {
        custom: [
          {
            cap: 1,
            period: 432000000,
          },
        ],
        lifetime: 3,
      },
      skip_in_tests:
        "it's not tested in automation and might pop up unexpectedly during review checker tests",
    },
    {
      // "Callout 4" in the Fakespot Figma spec, for rediscoverability experiment 2.
      id: "FAKESPOT_CALLOUT_DISABLED_AUTO_OPEN",
      template: "feature_callout",
      content: {
        id: "FAKESPOT_CALLOUT_DISABLED_AUTO_OPEN",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "FAKESPOT_CALLOUT_DISABLED_AUTO_OPEN",
            anchors: [
              {
                selector: "#shopping-sidebar-button",
                panel_position: {
                  anchor_attachment: "bottomcenter",
                  callout_attachment: "topright",
                },
                no_open_on_anchor: true,
              },
            ],
            content: {
              position: "callout",
              width: "401px",
              title: {
                string_id: "shopping-callout-disabled-auto-open-title",
              },
              subtitle: {
                string_id: "shopping-callout-disabled-auto-open-subtitle",
                letterSpacing: "0",
              },
              logo: {
                imageURL:
                  "chrome://browser/content/shopping/assets/priceTagButtonCallout.svg",
                height: "214px",
              },
              dismiss_button: {
                action: { dismiss: true },
                size: "small",
                marginBlock: "28px 0",
                marginInline: "0 28px",
              },
              primary_button: {
                label: {
                  string_id: "shopping-callout-disabled-auto-open-button",
                  marginBlock: "0 -8px",
                },
                style: "secondary",
                action: {
                  dismiss: true,
                },
              },
              page_event_listeners: [
                {
                  params: {
                    type: "click",
                    selectors: "#shopping-sidebar-button",
                  },
                  action: { dismiss: true },
                },
              ],
            },
          },
        ],
      },
      priority: 1,
      // Auto-open feature flag is enabled; User disabled auto-open behavior; User is opted in; Has not opted out of CFRs.
      targeting: `'browser.shopping.experience2023.autoOpen.enabled' | preferenceValue == true && 'browser.shopping.experience2023.autoOpen.userEnabled' | preferenceValue == false && 'browser.shopping.experience2023.optedIn' | preferenceValue == 1 && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue != false`,
      trigger: {
        id: "preferenceObserver",
        params: ["browser.shopping.experience2023.autoOpen.userEnabled"],
      },
      frequency: { lifetime: 1 },
      skip_in_tests:
        "it's not tested in automation and might pop up unexpectedly during review checker tests",
    },
    {
      // "Callout 5" in the Fakespot Figma spec, for rediscoverability experiment 2.
      id: "FAKESPOT_CALLOUT_OPTED_OUT_AUTO_OPEN",
      template: "feature_callout",
      content: {
        id: "FAKESPOT_CALLOUT_OPTED_OUT_AUTO_OPEN",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "FAKESPOT_CALLOUT_OPTED_OUT_AUTO_OPEN",
            anchors: [
              {
                selector: "#shopping-sidebar-button",
                panel_position: {
                  anchor_attachment: "bottomcenter",
                  callout_attachment: "topright",
                },
                no_open_on_anchor: true,
              },
            ],
            content: {
              position: "callout",
              width: "401px",
              title: {
                string_id: "shopping-callout-opted-out-title",
              },
              subtitle: {
                string_id: "shopping-callout-opted-out-subtitle",
                letterSpacing: "0",
              },
              logo: {
                imageURL:
                  "chrome://browser/content/shopping/assets/priceTagButtonCallout.svg",
                height: "214px",
              },
              dismiss_button: {
                action: { dismiss: true },
                size: "small",
                marginBlock: "28px 0",
                marginInline: "0 28px",
              },
              primary_button: {
                label: {
                  string_id: "shopping-callout-opted-out-button",
                  marginBlock: "0 -8px",
                },
                style: "secondary",
                action: {
                  dismiss: true,
                },
              },
              page_event_listeners: [
                {
                  params: {
                    type: "click",
                    selectors: "#shopping-sidebar-button",
                  },
                  action: { dismiss: true },
                },
              ],
            },
          },
        ],
      },
      priority: 1,
      // Auto-open feature flag is enabled; User has opted out; Has not opted out of CFRs; Integrated sidebar is false.
      targeting: `'browser.shopping.experience2023.autoOpen.enabled' | preferenceValue == true && 'browser.shopping.experience2023.optedIn' | preferenceValue == 2 && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue != false && !'browser.shopping.experience2023.integratedSidebar' | preferenceValue`,
      trigger: {
        id: "preferenceObserver",
        params: ["browser.shopping.experience2023.optedIn"],
      },
      frequency: { lifetime: 1 },
      skip_in_tests:
        "it's not tested in automation and might pop up unexpectedly during review checker tests",
    },

    // cookie banner reduction onboarding
    {
      id: "CFR_COOKIEBANNER",
      groups: ["cfr"],
      template: "feature_callout",
      content: {
        id: "CFR_COOKIEBANNER",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "COOKIEBANNER_CALLOUT",
            anchors: [
              {
                selector: "#tracking-protection-icon-container",
                panel_position: {
                  callout_attachment: "topleft",
                  anchor_attachment: "bottomcenter",
                },
              },
            ],
            content: {
              position: "callout",
              autohide: true,
              title: {
                string_id: "cookie-banner-blocker-onboarding-header",
                paddingInline: "12px 0",
              },
              subtitle: {
                string_id: "cookie-banner-blocker-onboarding-body",
                paddingInline: "34px 0",
              },
              title_logo: {
                alignment: "top",
                height: "20px",
                width: "20px",
                imageURL:
                  "chrome://browser/skin/controlcenter/3rdpartycookies-blocked.svg",
              },
              dismiss_button: {
                size: "small",
                action: { dismiss: true },
              },
              additional_button: {
                label: {
                  string_id: "cookie-banner-blocker-onboarding-learn-more",
                  marginInline: "34px 0",
                },
                style: "link",
                alignment: "start",
                action: {
                  type: "OPEN_URL",
                  data: {
                    args: "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/cookie-banner-reduction",
                    where: "tabshifted",
                  },
                },
              },
            },
          },
        ],
      },
      frequency: {
        lifetime: 1,
      },
      skip_in_tests: "it's not tested in automation",
      trigger: {
        id: "cookieBannerHandled",
      },
      targeting: `'cookiebanners.ui.desktop.enabled'|preferenceValue == true && 'cookiebanners.ui.desktop.showCallout'|preferenceValue == true && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue != false`,
    },
    {
      // "Callout 4A" in the Review Checker Sidebar Migration Figma spec
      // User is opted into Review Checker and decides to opt out of auto open
      // Sidebar is set to expand and collapse
      // Horizontal tabs
      id: "REVIEW_CHECKER_EXPAND_COLLAPSE_DISABLED_AUTO_OPEN",
      template: "feature_callout",
      content: {
        id: "REVIEW_CHECKER_EXPAND_COLLAPSE_DISABLED_AUTO_OPEN",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "REVIEW_CHECKER_EXPAND_COLLAPSE_DISABLED_AUTO_OPEN_HORIZONTAL",
            anchors: [
              {
                selector:
                  "#sidebar-main:not([sidebar-positionend]) > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar']",
                panel_position: {
                  anchor_attachment: "rightcenter",
                  callout_attachment: "topleft",
                },
                no_open_on_anchor: true,
              },
              {
                selector:
                  "#sidebar-main[sidebar-positionend] > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar']",
                panel_position: {
                  anchor_attachment: "leftcenter",
                  callout_attachment: "topright",
                },
                no_open_on_anchor: true,
              },
            ],
            content: {
              position: "callout",
              width: "401px",
              title: {
                string_id: "shopping-integrated-callout-sidebar-closed-title",
              },
              subtitle: {
                string_id:
                  "shopping-integrated-callout-sidebar-closed-subtitle",
                letterSpacing: "0",
              },
              logo: {
                imageURL:
                  "chrome://browser/content/shopping/assets/reviewCheckerCalloutPriceTag.svg",
                height: "195px",
              },
              dismiss_button: {
                action: { dismiss: true },
                size: "small",
                marginBlock: "20px 0",
                marginInline: "0 28px",
                background: true,
              },
            },
          },
        ],
      },
      priority: 1,
      // Auto-open feature flag is enabled; User disabled auto-open behavior; User is opted in; Has not opted out of CFRs; integrated sidebar is enabled; new sidebar is active; Sidebar is visible; Callout 6 has not been shown within 24 hrs;
      targeting: `'browser.shopping.experience2023.autoOpen.enabled' | preferenceValue && !'browser.shopping.experience2023.autoOpen.userEnabled' | preferenceValue && 'browser.shopping.experience2023.optedIn' | preferenceValue == 1 && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue && 'browser.shopping.experience2023.integratedSidebar' | preferenceValue && 'sidebar.revamp' | preferenceValue && isSidebarVisible && !'sidebar.verticalTabs' | preferenceValue && !(((currentDate|date - messageImpressions.REVIEW_CHECKER_SIDEBAR_CLOSED[messageImpressions.REVIEW_CHECKER_SIDEBAR_CLOSED | length - 1]) / 3600000) < 24)`,
      trigger: { id: "reviewCheckerSidebarClosedCallout" },
      frequency: { lifetime: 1 },
      skip_in_tests:
        "it's not tested in automation and might pop up unexpectedly during review checker tests",
    },
    {
      // "Callout 4A" in the Reivew Checker Sidebar Migration Figma spec
      // User is opted into Review Checker and decides to opt out of auto open
      // Sidebar is set to expand and collapse
      // Vertical tabs
      id: "REVIEW_CHECKER_EXPAND_COLLAPSE_DISABLED_AUTO_OPEN",
      template: "feature_callout",
      content: {
        id: "REVIEW_CHECKER_EXPAND_COLLAPSE_DISABLED_AUTO_OPEN",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "REVIEW_CHECKER_EXPAND_COLLAPSE_DISABLED_AUTO_OPEN_VERTICAL",
            anchors: [
              {
                selector:
                  "#sidebar-main:not([sidebar-positionend]) > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar'].tools-overflow",
                panel_position: {
                  anchor_attachment: "topcenter",
                  callout_attachment: "bottomleft",
                },
                no_open_on_anchor: true,
              },
              {
                selector:
                  "#sidebar-main[sidebar-positionend] > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar'].tools-overflow",
                panel_position: {
                  anchor_attachment: "topcenter",
                  callout_attachment: "bottomright",
                },
                no_open_on_anchor: true,
              },
              {
                selector:
                  "#sidebar-main:not([sidebar-positionend]) > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar']",
                panel_position: {
                  anchor_attachment: "rightcenter",
                  callout_attachment: "bottomleft",
                },
                no_open_on_anchor: true,
              },
              {
                selector:
                  "#sidebar-main[sidebar-positionend] > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar']",
                panel_position: {
                  anchor_attachment: "leftcenter",
                  callout_attachment: "bottomright",
                },
                no_open_on_anchor: true,
              },
            ],
            content: {
              position: "callout",
              width: "401px",
              title: {
                string_id: "shopping-integrated-callout-sidebar-closed-title",
              },
              subtitle: {
                string_id:
                  "shopping-integrated-callout-sidebar-closed-subtitle",
                letterSpacing: "0",
              },
              logo: {
                imageURL:
                  "chrome://browser/content/shopping/assets/reviewCheckerCalloutPriceTag.svg",
                height: "195px",
              },
              dismiss_button: {
                action: { dismiss: true },
                size: "small",
                marginBlock: "20px 0",
                marginInline: "0 28px",
                background: true,
              },
            },
          },
        ],
      },
      priority: 1,
      // Auto-open feature flag is enabled; User disabled auto-open behavior; User is opted in; Has not opted out of CFRs; integrated sidebar is enabled; new sidebar is active; Sidebar is visible; Callout 6 has not been shown within 24 hrs;
      targeting: `'browser.shopping.experience2023.autoOpen.enabled' | preferenceValue && !'browser.shopping.experience2023.autoOpen.userEnabled' | preferenceValue && 'browser.shopping.experience2023.optedIn' | preferenceValue == 1 && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue && 'browser.shopping.experience2023.integratedSidebar' | preferenceValue && 'sidebar.revamp' | preferenceValue && isSidebarVisible && 'sidebar.verticalTabs' | preferenceValue && !(((currentDate|date - messageImpressions.REVIEW_CHECKER_SIDEBAR_CLOSED[messageImpressions.REVIEW_CHECKER_SIDEBAR_CLOSED | length - 1]) / 3600000) < 24)`,
      trigger: { id: "reviewCheckerSidebarClosedCallout" },
      frequency: { lifetime: 1 },
      skip_in_tests:
        "it's not tested in automation and might pop up unexpectedly during review checker tests",
    },
    {
      // "Callout 4B" in the Review Checker Sidebar Migration Figma spec
      // User is opted into Review Checker and decides to opt out of auto open
      // Sidebar is set to Show and hide
      id: "REVIEW_CHECKER_SHOW_HIDE_DISABLED_AUTO_OPEN",
      template: "feature_callout",
      content: {
        id: "REVIEW_CHECKER_SHOW_HIDE_DISABLED_AUTO_OPEN",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "REVIEW_CHECKER_SHOW_HIDE_DISABLED_AUTO_OPEN",
            anchors: [
              {
                selector: "#sidebar-button",
                panel_position: {
                  anchor_attachment: "bottomcenter",
                  callout_attachment: "topleft",
                },
                no_open_on_anchor: true,
              },
            ],
            content: {
              position: "callout",
              width: "401px",
              title: {
                string_id: "shopping-integrated-callout-sidebar-closed-title",
              },
              subtitle: {
                string_id:
                  "shopping-integrated-callout-no-logo-sidebar-closed-subtitle",
                letterSpacing: "0",
              },
              dismiss_button: {
                action: { dismiss: true },
                size: "small",
              },
            },
          },
        ],
      },
      priority: 1,
      // Auto-open feature flag is enabled; User disabled auto-open behavior; User is opted in; Has not opted out of CFRs; integrated sidebar is enabled; new sidebar is active; Sidebar is not visible; Callout 6 has not shown within 24 hrs;
      targeting: `'browser.shopping.experience2023.autoOpen.enabled' | preferenceValue == true && 'browser.shopping.experience2023.autoOpen.userEnabled' | preferenceValue == false && 'browser.shopping.experience2023.optedIn' | preferenceValue == 1 && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue != false && 'browser.shopping.experience2023.integratedSidebar' | preferenceValue && 'sidebar.revamp' | preferenceValue && !isSidebarVisible && !(((currentDate|date - messageImpressions.REVIEW_CHECKER_SIDEBAR_CLOSED[messageImpressions.REVIEW_CHECKER_SIDEBAR_CLOSED | length - 1]) / 3600000) < 24)`,
      trigger: {
        id: "reviewCheckerSidebarClosedCallout",
      },
      frequency: { lifetime: 1 },
      skip_in_tests:
        "it's not tested in automation and might pop up unexpectedly during review checker tests",
    },
    {
      // "Callout 5" in the Review Checker Sidebar Migration Figma spec
      // Confirm settings update to turn off Review Checker and make sure users know how to get back to Review Checker
      // Horizontal tabs
      id: "REVIEW_CHECKER_INTEGRATED_SHOW_OPTED_OUT",
      template: "feature_callout",
      content: {
        id: "REVIEW_CHECKER_INTEGRATED_SHOW_OPTED_OUT",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "REVIEW_CHECKER_INTEGRATED_SHOW_OPTED_OUT_HORIZONTAL",
            anchors: [
              {
                selector:
                  "#sidebar-main:not([sidebar-positionend]) > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar']",
                panel_position: {
                  anchor_attachment: "rightcenter",
                  callout_attachment: "topleft",
                },
                no_open_on_anchor: true,
              },
              {
                selector:
                  "#sidebar-main[sidebar-positionend] > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar']",
                panel_position: {
                  anchor_attachment: "leftcenter",
                  callout_attachment: "topright",
                },
                no_open_on_anchor: true,
              },
            ],
            content: {
              position: "callout",
              width: "401px",
              title: {
                string_id: "shopping-integrated-callout-opted-out-title",
              },
              subtitle: {
                string_id: "shopping-integrated-callout-opted-out-subtitle",
                letterSpacing: "0",
              },
              logo: {
                imageURL:
                  "chrome://browser/content/shopping/assets/reviewCheckerCalloutPriceTag.svg",
                height: "195px",
              },
              dismiss_button: {
                action: { dismiss: true },
                size: "small",
                marginBlock: "20px 0",
                marginInline: "0 28px",
                background: true,
              },
            },
          },
        ],
      },
      priority: 1,
      // User has opted out; Has not opted out of CFRs; Integrated sidebar is enabled; Sidebar revamp is enabled.
      targeting: `'browser.shopping.experience2023.optedIn' | preferenceValue == 2 && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue && 'browser.shopping.experience2023.integratedSidebar' | preferenceValue && 'sidebar.revamp' | preferenceValue && !'sidebar.verticalTabs' | preferenceValue`,
      trigger: {
        id: "preferenceObserver",
        params: ["browser.shopping.experience2023.optedIn"],
      },
      frequency: { lifetime: 1 },
      skip_in_tests:
        "it's not tested in automation and might pop up unexpectedly during review checker tests",
    },
    {
      // "Callout 5" in the Review Checker Sidebar Migration Figma spec
      // Confirm settings update to turn off Review Checker and make sure users know how to get back to Review Checker
      // Vertical tabs
      id: "REVIEW_CHECKER_INTEGRATED_SHOW_OPTED_OUT",
      template: "feature_callout",
      content: {
        id: "REVIEW_CHECKER_INTEGRATED_SHOW_OPTED_OUT",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "REVIEW_CHECKER_INTEGRATED_SHOW_OPTED_OUT_VERTICAL",
            anchors: [
              {
                selector:
                  "#sidebar-main:not([sidebar-positionend]) > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar'].tools-overflow",
                panel_position: {
                  anchor_attachment: "topcenter",
                  callout_attachment: "bottomleft",
                },
                no_open_on_anchor: true,
              },
              {
                selector:
                  "#sidebar-main[sidebar-positionend] > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar'].tools-overflow",
                panel_position: {
                  anchor_attachment: "topcenter",
                  callout_attachment: "bottomright",
                },
                no_open_on_anchor: true,
              },
              {
                selector:
                  "#sidebar-main:not([sidebar-positionend]) > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar']",
                panel_position: {
                  anchor_attachment: "rightcenter",
                  callout_attachment: "bottomleft",
                },
                no_open_on_anchor: true,
              },
              {
                selector:
                  "#sidebar-main[sidebar-positionend] > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar']",
                panel_position: {
                  anchor_attachment: "leftcenter",
                  callout_attachment: "bottomright",
                },
                no_open_on_anchor: true,
              },
            ],
            content: {
              position: "callout",
              width: "401px",
              title: {
                string_id: "shopping-integrated-callout-opted-out-title",
              },
              subtitle: {
                string_id: "shopping-integrated-callout-opted-out-subtitle",
                letterSpacing: "0",
              },
              logo: {
                imageURL:
                  "chrome://browser/content/shopping/assets/reviewCheckerCalloutPriceTag.svg",
                height: "195px",
              },
              dismiss_button: {
                action: { dismiss: true },
                size: "small",
                marginBlock: "20px 0",
                marginInline: "0 28px",
                background: true,
              },
            },
          },
        ],
      },
      priority: 1,
      // User has opted out; Has not opted out of CFRs; Integrated sidebar is enabled; Sidebar revamp is enabled.
      targeting: `'browser.shopping.experience2023.optedIn' | preferenceValue == 2 && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue && 'browser.shopping.experience2023.integratedSidebar' | preferenceValue && 'sidebar.revamp' | preferenceValue && 'sidebar.verticalTabs' | preferenceValue`,
      trigger: {
        id: "preferenceObserver",
        params: ["browser.shopping.experience2023.optedIn"],
      },
      frequency: { lifetime: 1 },
      skip_in_tests:
        "it's not tested in automation and might pop up unexpectedly during review checker tests",
    },
    {
      // "Callout 6" in the Review Checker Figma spec
      // Explains where to find Review Checker after closing the sidebar with the X button
      // Horizontal tabs layout
      id: "REVIEW_CHECKER_SIDEBAR_CLOSED",
      template: "feature_callout",
      content: {
        id: "REVIEW_CHECKER_SIDEBAR_CLOSED",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "REVIEW_CHECKER_SIDEBAR_CLOSED_HORIZONTAL",
            anchors: [
              {
                selector:
                  "#sidebar-main:not([sidebar-positionend]) > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar']",
                panel_position: {
                  anchor_attachment: "rightcenter",
                  callout_attachment: "topleft",
                },
                no_open_on_anchor: true,
              },
              {
                selector:
                  "#sidebar-main[sidebar-positionend] > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar']",
                panel_position: {
                  anchor_attachment: "leftcenter",
                  callout_attachment: "topright",
                },
                no_open_on_anchor: true,
              },
            ],
            content: {
              position: "callout",
              width: "401px",
              title: {
                string_id: "shopping-integrated-callout-sidebar-closed-title",
              },
              subtitle: {
                string_id:
                  "shopping-integrated-callout-sidebar-closed-subtitle",
                letterSpacing: "0",
              },
              logo: {
                imageURL:
                  "chrome://browser/content/shopping/assets/reviewCheckerCalloutPriceTag.svg",
                height: "195px",
              },
              dismiss_button: {
                action: { dismiss: true },
                size: "small",
                marginBlock: "20px 0",
                marginInline: "0 28px",
                background: true,
              },
            },
          },
        ],
      },
      priority: 1,
      // Has not opted out of CFRs; Review Checker integrated sidebar is enabled; sidebar revamp is enabled; user is opted in to review checker; Using horizontal tabs; Neither Callout 4A or 4B has shown within 24 hrs;
      targeting: `'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue && 'browser.shopping.experience2023.integratedSidebar' | preferenceValue && 'sidebar.revamp' | preferenceValue && 'browser.shopping.experience2023.optedIn' | preferenceValue == 1 && isReviewCheckerInSidebarClosed && isSidebarVisible && !'sidebar.verticalTabs' | preferenceValue && !(((currentDate|date - messageImpressions.REVIEW_CHECKER_EXPAND_COLLAPSE_DISABLED_AUTO_OPEN[messageImpressions.REVIEW_CHECKER_EXPAND_COLLAPSE_DISABLED_AUTO_OPEN | length - 1]) / 3600000) < 24) && !(((currentDate|date - messageImpressions.REVIEW_CHECKER_SHOW_HIDE_DISABLED_AUTO_OPEN[messageImpressions.REVIEW_CHECKER_SHOW_HIDE_DISABLED_AUTO_OPEN | length - 1]) / 3600000) < 24)`,
      trigger: {
        id: "reviewCheckerSidebarClosedCallout",
      },
      frequency: { lifetime: 1 },
      skip_in_tests:
        "it's not tested in automation and might pop up unexpectedly during review checker tests",
    },
    {
      // "Callout 6" in the Review Checker Figma spec
      // Explains where to find Review Checker after closing the sidebar with the X button
      // Vertical tabs layout
      id: "REVIEW_CHECKER_SIDEBAR_CLOSED",
      template: "feature_callout",
      content: {
        id: "REVIEW_CHECKER_SIDEBAR_CLOSED",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "REVIEW_CHECKER_SIDEBAR_CLOSED_VERTICAL",
            anchors: [
              {
                selector:
                  "#sidebar-main:not([sidebar-positionend]) > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar'].tools-overflow",
                panel_position: {
                  anchor_attachment: "topcenter",
                  callout_attachment: "bottomleft",
                },
                no_open_on_anchor: true,
              },
              {
                selector:
                  "#sidebar-main[sidebar-positionend] > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar'].tools-overflow",
                panel_position: {
                  anchor_attachment: "topcenter",
                  callout_attachment: "bottomright",
                },
                no_open_on_anchor: true,
              },
              {
                selector:
                  "#sidebar-main:not([sidebar-positionend]) > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar']",
                panel_position: {
                  anchor_attachment: "rightcenter",
                  callout_attachment: "bottomleft",
                },
                no_open_on_anchor: true,
              },
              {
                selector:
                  "#sidebar-main[sidebar-positionend] > sidebar-main::%shadow% .tools-and-extensions::%shadow% moz-button[view='viewReviewCheckerSidebar']",
                panel_position: {
                  anchor_attachment: "leftcenter",
                  callout_attachment: "bottomright",
                },
                no_open_on_anchor: true,
              },
            ],
            content: {
              position: "callout",
              width: "401px",
              title: {
                string_id: "shopping-integrated-callout-sidebar-closed-title",
              },
              subtitle: {
                string_id:
                  "shopping-integrated-callout-sidebar-closed-subtitle",
                letterSpacing: "0",
              },
              logo: {
                imageURL:
                  "chrome://browser/content/shopping/assets/reviewCheckerCalloutPriceTag.svg",
                height: "195px",
              },
              dismiss_button: {
                action: { dismiss: true },
                size: "small",
                marginBlock: "20px 0",
                marginInline: "0 28px",
                background: true,
              },
            },
          },
        ],
      },
      priority: 1,
      // Has not opted out of CFRs; Review Checker integrated sidebar is enabled; sidebar revamp is enabled; user is opted in to review checker; Vertical tabs is enabled; Neither callout 4A or 4B has shown within 24 hrs;
      targeting: `'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue && 'browser.shopping.experience2023.integratedSidebar' | preferenceValue && 'sidebar.revamp' | preferenceValue && 'browser.shopping.experience2023.optedIn' | preferenceValue == 1 && isSidebarVisible && isReviewCheckerInSidebarClosed && 'sidebar.verticalTabs' | preferenceValue && !(((currentDate|date - messageImpressions.REVIEW_CHECKER_EXPAND_COLLAPSE_DISABLED_AUTO_OPEN[messageImpressions.REVIEW_CHECKER_EXPAND_COLLAPSE_DISABLED_AUTO_OPEN | length - 1]) / 3600000) < 24) && !(((currentDate|date - messageImpressions.REVIEW_CHECKER_SHOW_HIDE_DISABLED_AUTO_OPEN[messageImpressions.REVIEW_CHECKER_SHOW_HIDE_DISABLED_AUTO_OPEN | length - 1]) / 3600000) < 24)`,
      trigger: {
        id: "reviewCheckerSidebarClosedCallout",
      },
      frequency: { lifetime: 1 },
      skip_in_tests:
        "it's not tested in automation and might pop up unexpectedly during review checker tests",
    },
    {
      id: "FX_VIEW_DISCOVERABILITY_ALL_USERS",
      template: "feature_callout",
      groups: ["cfr"],
      content: {
        id: "FX_VIEW_DISCOVERABILITY_ALL_USERS",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        screens: [
          {
            id: "FX_VIEW_DISCOVERABILITY_ALL_USERS_SCREEN",
            anchors: [
              {
                selector: "#firefox-view-button",
                panel_position: {
                  anchor_attachment: "bottomcenter",
                  callout_attachment: "topleft",
                },
                no_open_on_anchor: true,
                arrow_width: "15.5563",
              },
            ],
            content: {
              position: "callout",
              width: "342px",
              padding: 16,
              page_event_listeners: [
                {
                  params: {
                    type: "click",
                    selectors: "#firefox-view-button",
                  },
                  action: {
                    dismiss: true,
                  },
                },
              ],
              title_logo: {
                imageURL:
                  "chrome://browser/content/asrouter/assets/fox-question-mark-icon.svg",
                width: "25px",
                height: "29px",
                marginInline: "4px 14px",
                alignment: "top",
              },
              title: {
                string_id: "fx-view-discoverability-title",
                marginInline: "0 16px",
              },
              subtitle: {
                string_id: "fx-view-discoverability-subtitle",
                paddingInline: "34px 0",
                marginBlock: "-8px -4px",
              },
              additional_button: {
                label: {
                  string_id: "fx-view-discoverability-secondary-button-label",
                },
                style: "secondary",
                action: {
                  type: "BLOCK_MESSAGE",
                  data: {
                    id: "FX_VIEW_DISCOVERABILITY_ALL_USERS",
                  },
                  dismiss: true,
                },
              },
              secondary_button: {
                label: {
                  string_id: "fx-view-discoverability-primary-button-label",
                },
                style: "primary",
                action: {
                  type: "OPEN_FIREFOX_VIEW",
                  navigate: true,
                },
              },
              submenu_button: {
                submenu: [
                  {
                    type: "action",
                    label: {
                      string_id: "split-dismiss-button-dont-show-option",
                    },
                    action: {
                      type: "BLOCK_MESSAGE",
                      data: {
                        id: "FX_VIEW_DISCOVERABILITY_ALL_USERS",
                      },
                      dismiss: true,
                    },
                    id: "block_recommendation",
                  },
                  {
                    type: "action",
                    label: {
                      string_id: "split-dismiss-button-show-fewer-option",
                    },
                    action: {
                      type: "MULTI_ACTION",
                      dismiss: true,
                      data: {
                        actions: [
                          {
                            type: "SET_PREF",
                            data: {
                              pref: {
                                name: "messaging-system-action.firefox-view-recommendations",
                                value: true,
                              },
                            },
                          },
                          {
                            type: "BLOCK_MESSAGE",
                            data: {
                              id: "FX_VIEW_DISCOVERABILITY_ALL_USERS",
                            },
                          },
                        ],
                      },
                    },
                    id: "show_fewer_recommendations",
                  },
                  {
                    type: "separator",
                  },
                  {
                    type: "action",
                    label: {
                      string_id: "split-dismiss-button-manage-settings-option",
                    },
                    action: {
                      type: "OPEN_ABOUT_PAGE",
                      data: {
                        args: "preferences#general-cfrfeatures",
                        where: "tab",
                      },
                      dismiss: true,
                    },
                    id: "manage_settings",
                  },
                ],
                attached_to: "additional_button",
              },
            },
          },
        ],
      },
      frequency: {
        lifetime: 1,
      },
      skip_in_tests: "it's not tested in automation",
      targeting:
        "!isMajorUpgrade && !willShowDefaultPrompt && !activeNotifications && previousSessionEnd && fxViewButtonAreaType != null && tabsClosedCount >= 5 && (currentDate|date - profileAgeCreated|date) / 86400000 >= 7 && 'browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features' | preferenceValue != false",
      trigger: {
        id: "nthTabClosed",
      },
    },
  ];
  messages = add24HourImpressionJEXLTargeting(
    ["FIREFOX_VIEW_TAB_PICKUP_REMINDER"],
    "FIREFOX_VIEW",
    messages
  );
  return messages;
};

export const FeatureCalloutMessages = {
  getMessages() {
    return MESSAGES();
  },
};
