/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { GeckoViewUtils } from "resource://gre/modules/GeckoViewUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  GeckoViewPrompter:
    "moz-src:///mobile/shared/components/geckoview/GeckoViewPrompter.sys.mjs",
});

export const GeckoViewIdentityCredential = {
  async onShowProviderPrompt(aBrowser, providers, resolve, reject) {
    const prompt = new lazy.GeckoViewPrompter(aBrowser.documentGlobal);
    debug`onShowProviderPrompt`;

    prompt.asyncShowPrompt(
      {
        type: "IdentityCredential:Select:Provider",
        providers,
      },
      result => {
        if (result && result.providerIndex != null) {
          debug`onShowProviderPrompt resolve with ${result.providerIndex}`;
          resolve(result.providerIndex);
        } else {
          debug`onShowProviderPrompt rejected`;
          reject();
        }
      }
    );
  },
  async onShowAccountsPrompt(aBrowser, accounts, resolve, reject) {
    const prompt = new lazy.GeckoViewPrompter(aBrowser.documentGlobal);
    debug`onShowAccountsPrompt`;

    prompt.asyncShowPrompt(
      {
        type: "IdentityCredential:Select:Account",
        accounts,
      },
      result => {
        if (result && result.accountIndex != null) {
          debug`onShowAccountsPrompt resolve with ${result.accountIndex}`;
          resolve(result.accountIndex);
        } else {
          debug`onShowAccountsPrompt rejected`;
          reject();
        }
      }
    );
  },
  async onShowPolicyPrompt(
    aBrowser,
    privacyPolicyUrl,
    termsOfServiceUrl,
    providerDomain,
    host,
    icon,
    resolve,
    reject
  ) {
    const prompt = new lazy.GeckoViewPrompter(aBrowser.documentGlobal);
    debug`onShowPolicyPrompt`;

    prompt.asyncShowPrompt(
      {
        type: "IdentityCredential:Show:Policy",
        privacyPolicyUrl,
        termsOfServiceUrl,
        providerDomain,
        host,
        icon,
      },
      result => {
        if (result && result.accept != null) {
          debug`onShowPolicyPrompt resolve with ${result.accept}`;
          resolve(result.accept);
        } else {
          debug`onShowPolicyPrompt rejected`;
          reject();
        }
      }
    );
  },
};

const { debug } = GeckoViewUtils.initLogging("GeckoViewIdentityCredential");
