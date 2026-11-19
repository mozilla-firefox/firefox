/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { GeckoViewUtils } from "resource://gre/modules/GeckoViewUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  EventDispatcher: "resource://gre/modules/Messaging.sys.mjs",
  IPPAndroidAuthProvider:
    "moz-src:///toolkit/components/ipprotection/fxa/IPPAndroidAuthProvider.sys.mjs",
  IPPAuthProvider:
    "moz-src:///toolkit/components/ipprotection/IPPAuthProvider.sys.mjs",
  IPPDummyAuthProvider:
    "moz-src:///toolkit/components/ipprotection/tests/IPPDummyAuthProvider.sys.mjs",
  IPPGpiAuthProvider:
    "moz-src:///toolkit/components/ipprotection/gpi/IPPGpiAuthProvider.sys.mjs",
  IPPProxyManager:
    "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
  IPProtectionServerlist:
    "moz-src:///toolkit/components/ipprotection/IPProtectionServerlist.sys.mjs",
  IPProtectionActivator:
    "moz-src:///toolkit/components/ipprotection/IPProtectionActivator.sys.mjs",
  IPProtectionService:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
});

const { debug } = GeckoViewUtils.initLoggingConsole("GeckoViewIPProtection");

function sendRequest(message, data) {
  debug`send ${message} ${data}`;
  lazy.EventDispatcher.instance.sendRequest(message, data);
}

const AUTH_PROVIDER_PREF = "toolkit.ipProtection.android.authProvider";

let initialized = false;

export const GeckoViewIPProtection = {
  // Events dispatched by components in toolkit/components/ipprotection.
  handleEvent(event) {
    let detail;
    switch (event.type) {
      case "IPPProxyManager:StateChanged": {
        const state = lazy.IPPProxyManager.state;
        detail = {
          state,
          errorType:
            state === "error" ? (lazy.IPPProxyManager.errorType ?? null) : null,
        };
        break;
      }
      case "IPPProxyManager:UsageChanged": {
        const { usage } = event.detail;
        detail = {
          remaining: Number(usage.remaining),
          max: Number(usage.max),
          resetTime: usage.reset?.toString() ?? null,
        };
        break;
      }
      case "IPProtectionServerlist:ListChanged": {
        sendRequest("GeckoView:IPProtection:ServerList:ListChanged", {
          countries: lazy.IPProtectionServerlist.countries,
        });
        return;
      }
      default:
        detail = event.detail;
    }
    sendRequest(`GeckoView:IPProtection:${event.type}`, detail);
  },

  // Events dispatched from IPProtectionController.java via EventDispatcher.
  onEvent(aEvent, aData, aCallback) {
    debug`recv ${aEvent} ${aData}`;

    switch (aEvent) {
      case "GeckoView:IPProtection:Init": {
        if (!initialized) {
          initialized = true;
          lazy.IPPProxyManager.addEventListener(
            "IPPProxyManager:StateChanged",
            GeckoViewIPProtection
          );
          lazy.IPPProxyManager.addEventListener(
            "IPPProxyManager:UsageChanged",
            GeckoViewIPProtection
          );
          lazy.IPProtectionService.addEventListener(
            "IPProtectionService:StateChanged",
            GeckoViewIPProtection
          );
          lazy.IPProtectionServerlist.addEventListener(
            "IPProtectionServerlist:ListChanged",
            GeckoViewIPProtection
          );
          let providerName = Services.prefs.getCharPref(AUTH_PROVIDER_PREF, "");
          if (!providerName) {
            providerName = aData?.isSignedIn ? "fxa" : "gpi";
            Services.prefs.setCharPref(AUTH_PROVIDER_PREF, providerName);
          }
          if (providerName === "fxa") {
            lazy.IPProtectionActivator.setAuthProvider(
              lazy.IPPAndroidAuthProvider
            );
          } else if (providerName === "test") {
            lazy.IPProtectionActivator.setAuthProvider(
              lazy.IPPDummyAuthProvider
            );
          } else {
            lazy.IPProtectionActivator.setAuthProvider(lazy.IPPGpiAuthProvider);
            lazy.IPProtectionActivator.setFallbackAuthProvider(
              lazy.IPPAndroidAuthProvider,
              () => Services.prefs.setCharPref(AUTH_PROVIDER_PREF, "fxa")
            );
          }
          lazy.IPProtectionActivator.init();
        }
        aCallback.onSuccess();
        break;
      }
      case "GeckoView:IPProtection:Uninit": {
        if (initialized) {
          initialized = false;
          lazy.IPPProxyManager.removeEventListener(
            "IPPProxyManager:StateChanged",
            GeckoViewIPProtection
          );
          lazy.IPPProxyManager.removeEventListener(
            "IPPProxyManager:UsageChanged",
            GeckoViewIPProtection
          );
          lazy.IPProtectionService.removeEventListener(
            "IPProtectionService:StateChanged",
            GeckoViewIPProtection
          );
          lazy.IPProtectionServerlist.removeEventListener(
            "IPProtectionServerlist:ListChanged",
            GeckoViewIPProtection
          );
          lazy.IPProtectionActivator.uninit();
          lazy.IPProtectionActivator.removeHelpers();

          lazy.IPProtectionActivator.setAuthProvider(
            new lazy.IPPAuthProvider()
          );
          lazy.IPPProxyManager.updateState();
        }
        aCallback.onSuccess();
        break;
      }
      case "GeckoView:IPProtection:IPProtectionService:GetState": {
        aCallback.onSuccess({ state: lazy.IPProtectionService.state });
        break;
      }
      case "GeckoView:IPProtection:IPPProxyManager:GetState": {
        const state = lazy.IPPProxyManager.state;
        aCallback.onSuccess({
          state,
          errorType:
            state === "error" ? (lazy.IPPProxyManager.errorType ?? null) : null,
        });
        break;
      }
      case "GeckoView:IPProtection:ServerList:GetCountryList": {
        lazy.IPProtectionServerlist.maybeFetchList()
          .then(changed => {
            // maybeFetchList already dispatches ListChanged (forwarded to the
            // delegate) when the list changed; only push manually otherwise so
            // getCountryList() delivers the current list exactly once.
            if (!changed) {
              sendRequest("GeckoView:IPProtection:ServerList:ListChanged", {
                countries: lazy.IPProtectionServerlist.countries,
              });
            }
            aCallback.onSuccess();
          })
          .catch(err => {
            aCallback.onError(
              typeof err === "string" ? err : (err?.message ?? "generic-error")
            );
          });
        break;
      }
      case "GeckoView:IPProtection:Activate": {
        // When the proxy is already active, an Activate request is treated as a
        // request to switch the connection to the given country.
        if (lazy.IPPProxyManager.state === "active") {
          const { switched, error } =
            lazy.IPPProxyManager.switch(aData?.country) ?? {};
          if (error) {
            aCallback.onError(error);
          } else if (switched) {
            aCallback.onSuccess();
          } else {
            aCallback.onError("generic-error");
          }
          break;
        }
        lazy.IPPProxyManager.start(
          aData?.userAction ?? true,
          aData?.inPrivateBrowsing ?? false,
          aData?.country
        )
          .then(({ started, error } = {}) => {
            if (started) {
              aCallback.onSuccess();
            } else {
              aCallback.onError(error ?? "generic-error");
            }
          })
          .catch(err => {
            aCallback.onError(
              typeof err === "string" ? err : (err?.message ?? "generic-error")
            );
          });
        break;
      }
      case "GeckoView:IPProtection:Enroll": {
        lazy.IPProtectionService.authProvider
          .enroll()
          .then(({ isEnrolledAndEntitled, error } = {}) => {
            aCallback.onSuccess({
              isEnrolledAndEntitled: !!isEnrolledAndEntitled,
              error: error ?? null,
            });
          })
          .catch(err => {
            aCallback.onError(
              typeof err === "string" ? err : (err?.message ?? "generic-error")
            );
          });
        break;
      }
      case "GeckoView:IPProtection:Deactivate": {
        lazy.IPPProxyManager.stop()
          .then(() => {
            aCallback.onSuccess();
          })
          .catch(err => {
            aCallback.onError(
              typeof err === "string" ? err : (err?.message ?? "generic-error")
            );
          });
        break;
      }
      case "GeckoView:IPProtection:RefreshUsage": {
        lazy.IPPProxyManager.refreshUsage()
          .then(() => {
            aCallback.onSuccess();
          })
          .catch(err => {
            aCallback.onError(
              typeof err === "string" ? err : (err?.message ?? "generic-error")
            );
          });
        break;
      }
    }
  },
};
