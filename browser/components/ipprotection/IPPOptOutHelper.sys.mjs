/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  IPProtectionService:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
});

const OPTED_OUT_PREF = "browser.ipProtection.optedOut";
const WIDGET_ID = "ipprotection-button";

/**
 * This class monitors the optedOut pref and if it sees an opted-out state, it
 * sets the state on IPProtectionService and removes the toolbar widget.
 * If the widget is added back to the toolbar, the opted-out state is cleared.
 */
class IPPOptedOutHelperSingleton {
  #widgetListener = {
    onWidgetAdded: widgetId => {
      if (widgetId == WIDGET_ID) {
        this.#clearOptedOut();
      }
    },
    onAreaReset: () => {
      if (lazy.CustomizableUI.getPlacementOfWidget(WIDGET_ID)) {
        this.#clearOptedOut();
      }
    },
  };

  init() {
    Services.prefs.addObserver(OPTED_OUT_PREF, this);
    lazy.CustomizableUI.addListener(this.#widgetListener);
  }

  uninit() {
    Services.prefs.removeObserver(OPTED_OUT_PREF, this);
    lazy.CustomizableUI.removeListener(this.#widgetListener);
  }

  #clearOptedOut() {
    if (this.optedOut) {
      Services.prefs.setBoolPref(OPTED_OUT_PREF, false);
    }
  }

  initOnStartupCompleted() {}

  get optedOut() {
    return Services.prefs.getBoolPref(OPTED_OUT_PREF, false);
  }

  observe(_subject, _topic, _data) {
    lazy.IPProtectionService.updateState();
    if (this.optedOut) {
      lazy.CustomizableUI.removeWidgetFromArea(WIDGET_ID);
    }
  }
}

const IPPOptOutHelper = new IPPOptedOutHelperSingleton();

export { IPPOptOutHelper };
