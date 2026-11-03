/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  Spotlight: "resource:///modules/asrouter/Spotlight.sys.mjs",
});

const ACCEPTED_TOPIC = "smartwindow-tou-accepted";

const TOU_GUEST_MESSAGE = {
  id: "AI_WINDOW_TOU_GUEST_MODAL",
  template: "spotlight",
  content: {
    template: "multistage",
    id: "AI_WINDOW_TOU_GUEST_MODAL",
    modal: "window",
    requireAction: true,
    disableEscClose: true,
    screens: [
      {
        id: "AI_WINDOW_TOU_GUEST_MODAL",
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
            string_id: "smartwindow-guest-tou-title",
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
                args: "https://www.mozilla.org/about/legal/terms/smart-window/",
              },
            },
          },
          privacy_notice: {
            action: {
              type: "OPEN_URL",
              data: {
                where: "chromeless",
                args: "https://www.mozilla.org/privacy/smart-window/",
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
              type: "ACCEPT_SMARTWINDOW_TOU",
              navigate: true,
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
              dismiss: true,
            },
          },
        },
      },
    ],
  },
};

const GUEST_UNAVAILABLE_MESSAGE = {
  id: "AI_WINDOW_GUEST_UNAVAILABLE_MODAL",
  template: "spotlight",
  content: {
    template: "multistage",
    id: "AI_WINDOW_GUEST_UNAVAILABLE_MODAL",
    modal: "window",
    screens: [
      {
        id: "AI_WINDOW_GUEST_UNAVAILABLE_MODAL",
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
            string_id: "smartwindow-guest-unavailable-title",
          },
          subtitle: {
            string_id: "smartwindow-guest-unavailable-body",
          },
          primary_button: {
            label: {
              string_id: "smartwindow-guest-unavailable-ok",
              paddingBlock: "4px",
            },
            style: "primary",
            action: {
              dismiss: true,
            },
          },
        },
      },
    ],
  },
};

export const ToUSpotlight = {
  /**
   * Shows the Smart Window Terms of Use Spotlight modal.
   *
   * @param {Browser} browser
   * @returns {Promise<boolean>} Whether the user accepted
   */
  async show(browser) {
    let accepted = false;
    const observer = () => {
      accepted = true;
    };
    Services.obs.addObserver(observer, ACCEPTED_TOPIC);
    try {
      await lazy.Spotlight.showSpotlightDialog(browser, TOU_GUEST_MESSAGE);
    } finally {
      Services.obs.removeObserver(observer, ACCEPTED_TOPIC);
    }
    return accepted;
  },

  /**
   * Shows a Spotlight modal telling the user that Smart Window is temporarily
   * unavailable, after the guest sign-in failed.
   *
   * @param {Browser} browser
   * @returns {Promise<boolean>} Resolves to true once the modal has been
   *   dismissed, or to false if it could not be shown (e.g. another dialog is
   *   already open or the window is closed).
   */
  showUnavailable(browser) {
    return lazy.Spotlight.showSpotlightDialog(
      browser,
      GUEST_UNAVAILABLE_MESSAGE
    );
  },
};
