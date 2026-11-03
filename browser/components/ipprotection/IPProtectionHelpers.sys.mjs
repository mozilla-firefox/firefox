/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Note: If you add or modify the list of helpers, make sure to update the
 * corresponding documentation in the `docs` folder as well.
 */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { IPProtectionActivator } from "moz-src:///toolkit/components/ipprotection/IPProtectionActivator.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  IPPAutoActivation:
    "moz-src:///toolkit/components/ipprotection/IPPAutoActivation.sys.mjs",
  IPProtection:
    "moz-src:///browser/components/ipprotection/IPProtection.sys.mjs",
  IPProtectionPanel:
    "moz-src:///browser/components/ipprotection/IPProtectionPanel.sys.mjs",
  IPProtectionService:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
  IPProtectionStates:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
  IPPFxaActivateAuthProvider:
    "moz-src:///toolkit/components/ipprotection/fxa/IPPFxaActivateAuthProvider.sys.mjs",
});

if (AppConstants.MOZ_ENTERPRISE) {
  ChromeUtils.defineESModuleGetters(lazy, {
    IPPEnterpriseAuthProvider:
      "moz-src:///toolkit/components/ipprotection/enterprise/IPPEnterpriseAuthProvider.sys.mjs",
  });
}
import { IPPUsageHelper } from "moz-src:///browser/components/ipprotection/IPPUsageHelper.sys.mjs";
import { IPPL10nHelper } from "moz-src:///browser/components/ipprotection/IPPL10nHelper.sys.mjs";
import { IPPOnboardingMessage } from "moz-src:///browser/components/ipprotection/IPPOnboardingMessageHelper.sys.mjs";
import { IPPOptOutHelper } from "moz-src:///browser/components/ipprotection/IPPOptOutHelper.sys.mjs";
import { IPProtectionAlertManager } from "moz-src:///browser/components/ipprotection/IPProtectionAlertManager.sys.mjs";
import { IPProtectionInfobarManager } from "moz-src:///browser/components/ipprotection/IPProtectionInfobarManager.sys.mjs";

/**
 * This simple class controls the UI activation/deactivation.
 */
class UIHelper {
  constructor() {
    this.handleEvent = this.#handleEvent.bind(this);
  }

  init() {
    lazy.IPProtectionService.addEventListener(
      "IPProtectionService:StateChanged",
      this.handleEvent
    );
  }

  initOnStartupCompleted() {}

  uninit() {
    lazy.IPProtectionService.removeEventListener(
      "IPProtectionService:StateChanged",
      this.handleEvent
    );
    lazy.IPProtection.uninit();
  }

  #handleEvent(_event) {
    const state = lazy.IPProtectionService.state;

    if (
      !lazy.IPProtection.isInitialized &&
      state !== lazy.IPProtectionStates.UNINITIALIZED &&
      state !== lazy.IPProtectionStates.UNAVAILABLE
    ) {
      lazy.IPProtection.init();
    }

    if (
      lazy.IPProtection.isInitialized &&
      (state === lazy.IPProtectionStates.UNINITIALIZED ||
        state === lazy.IPProtectionStates.UNAVAILABLE)
    ) {
      lazy.IPProtection.uninit();
    }
  }
}

function pickAuthProvider() {
  if (AppConstants.MOZ_ENTERPRISE) {
    return lazy.IPPEnterpriseAuthProvider;
  }
  return lazy.IPPFxaActivateAuthProvider;
}

const authProvider = pickAuthProvider();

IPProtectionActivator.addHelpers([
  IPPL10nHelper,
  IPPOnboardingMessage,
  IPPUsageHelper,
  new UIHelper(),
  IPPOptOutHelper,
  IPProtectionAlertManager,
  IPProtectionInfobarManager,
  ...authProvider.helpers,
]);

IPProtectionActivator.setAuthProvider(authProvider);

if (!AppConstants.MOZ_ENTERPRISE) {
  lazy.IPPAutoActivation.isPremium = () => lazy.IPProtectionPanel.isPremium();
}

export { IPProtectionActivator };
