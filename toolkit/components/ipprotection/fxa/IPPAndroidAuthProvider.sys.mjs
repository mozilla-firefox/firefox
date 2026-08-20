/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AUTH_ERRORS } from "moz-src:///toolkit/components/ipprotection/IPPAuthProvider.sys.mjs";
import { IPPFxaActivateAuthProviderSingleton } from "moz-src:///toolkit/components/ipprotection/fxa/IPPFxaActivateAuthProvider.sys.mjs";
import { IPPAndroidSignInWatcher } from "moz-src:///toolkit/components/ipprotection/fxa/IPPAndroidSignInWatcher.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  EventDispatcher: "resource://gre/modules/Messaging.sys.mjs",
});

/**
 * Android implementation of IPPFxaActivateAuthProviderSingleton.
 * Uses EventDispatcher to bridge sign-in state and token retrieval to the
 * Android layer. Tokens are requested on demand and never cached in Gecko.
 */
class IPPAndroidFxAAuthProviderSingleton extends IPPFxaActivateAuthProviderSingleton {
  constructor() {
    super(IPPAndroidSignInWatcher);
  }

  async getToken(abortSignal = null) {
    abortSignal?.throwIfAborted();
    const request = lazy.EventDispatcher.instance.sendRequestForResult(
      "GeckoView:IPProtection:GetToken"
    );
    let response;
    if (abortSignal) {
      const cleanup = new AbortController();
      const aborted = new Promise((_, reject) => {
        abortSignal.addEventListener(
          "abort",
          () => reject(abortSignal.reason),
          { once: true, signal: cleanup.signal }
        );
      });
      try {
        response = await Promise.race([request, aborted]);
      } finally {
        cleanup.abort();
      }
    } else {
      response = await request;
    }
    // IPProtectionController.java already rejects an empty or missing token, so
    // this only guards against that contract changing.
    const token = response?.token;
    if (!token) {
      throw AUTH_ERRORS.LOGIN_NEEDED;
    }
    return { token, [Symbol.dispose]: () => {} };
  }
}

const IPPAndroidAuthProvider = new IPPAndroidFxAAuthProviderSingleton();

export { IPPAndroidAuthProvider };
