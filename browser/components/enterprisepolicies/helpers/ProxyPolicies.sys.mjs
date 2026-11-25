/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const PREF_LOGLEVEL = "browser.policies.loglevel";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PolicyFailures: "resource://gre/modules/PoliciesHelpers.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", () => {
  let { ConsoleAPI } = ChromeUtils.importESModule(
    "resource://gre/modules/Console.sys.mjs"
  );
  return new ConsoleAPI({
    prefix: "ProxyPolicies",
    // tip: set maxLogLevel to "debug" and use log.debug() to create detailed
    // messages during development. See LOG_LEVELS in Console.sys.mjs for details.
    maxLogLevel: "error",
    maxLogLevelPref: PREF_LOGLEVEL,
  });
});

// Don't use const here because this is acessed by
// tests through the SystemGlobal object.
export var PROXY_TYPES_MAP = new Map([
  ["none", Ci.nsIProtocolProxyService.PROXYCONFIG_DIRECT],
  ["system", Ci.nsIProtocolProxyService.PROXYCONFIG_SYSTEM],
  ["manual", Ci.nsIProtocolProxyService.PROXYCONFIG_MANUAL],
  ["autoDetect", Ci.nsIProtocolProxyService.PROXYCONFIG_WPAD],
  ["autoConfig", Ci.nsIProtocolProxyService.PROXYCONFIG_PAC],
]);

let proxyPreferences = [
  "network.proxy.type",
  "network.proxy.autoconfig_url",
  "network.proxy.socks_remote_dns",
  "network.proxy.socks5_remote_dns",
  "signon.autologin.proxy",
  "network.proxy.socks_version",
  "network.proxy.no_proxies_on",
  "network.proxy.share_proxy_settings",
  "network.proxy.http",
  "network.proxy.http_port",
  "network.proxy.ssl",
  "network.proxy.ssl_port",
  "network.proxy.socks",
  "network.proxy.socks_port",
];

/**
 * Reports an operation of a policy that failed, so that the policy is flagged
 * as only partially applied in about:policies.
 *
 * @param {string} message A description of what failed.
 */
function reportFailure(message) {
  lazy.log.error(message);
  lazy.PolicyFailures.report("Proxy", message);
}

export var ProxyPolicies = {
  configureProxySettings(param, setPref) {
    if (param.Mode) {
      setPref("network.proxy.type", PROXY_TYPES_MAP.get(param.Mode));
    }

    if (param.AutoConfigURL) {
      setPref("network.proxy.autoconfig_url", param.AutoConfigURL.href);
    }

    if (param.UseProxyForDNS !== undefined) {
      setPref("network.proxy.socks_remote_dns", param.UseProxyForDNS);
      setPref("network.proxy.socks5_remote_dns", param.UseProxyForDNS);
    }

    if (param.AutoLogin !== undefined) {
      setPref("signon.autologin.proxy", param.AutoLogin);
    }

    if (param.SOCKSVersion !== undefined) {
      if (param.SOCKSVersion != 4 && param.SOCKSVersion != 5) {
        lazy.log.error("Invalid SOCKS version");
      } else {
        setPref("network.proxy.socks_version", param.SOCKSVersion);
      }
    }

    if (param.Passthrough !== undefined) {
      setPref("network.proxy.no_proxies_on", param.Passthrough);
    }

    if (param.UseHTTPProxyForAllProtocols !== undefined) {
      setPref(
        "network.proxy.share_proxy_settings",
        param.UseHTTPProxyForAllProtocols
      );
    }

    if (param.FTPProxy) {
      lazy.log.warn("FTPProxy support was removed in bug 1574475");
    }

    function setProxyHostAndPort(type, address) {
      // Prepend https just so we can use the URL parser
      // instead of parsing manually.
      let url = URL.parse(`https://${address}`);
      if (!url) {
        reportFailure(`Invalid address for ${type} proxy: ${address}`);
        return;
      }

      // The network.proxy.* prefs expect IPv6 hosts without brackets.
      setPref(
        `network.proxy.${type}`,
        url.hostname.replace(/^\[(.*)\]$/, "$1")
      );
      // The URL parser drops default ports, so also parse as http to
      // recover an explicit 443.
      let port = Number(url.port || URL.parse(`http://${address}`).port);
      setPref(`network.proxy.${type}_port`, port);
      if (!port) {
        reportFailure(`No port specified for ${type} proxy: ${address}`);
      }
    }

    if (param.HTTPProxy) {
      setProxyHostAndPort("http", param.HTTPProxy);

      // network.proxy.share_proxy_settings is a UI feature, not handled by the
      // network code. That pref only controls if the checkbox is checked, and
      // then we must manually set the other values.
      if (param.UseHTTPProxyForAllProtocols) {
        param.SSLProxy = param.HTTPProxy;
      }
    }

    if (param.SSLProxy) {
      setProxyHostAndPort("ssl", param.SSLProxy);
    }

    if (param.SOCKSProxy) {
      setProxyHostAndPort("socks", param.SOCKSProxy);
    }

    // All preferences should be locked regardless of whether or not a
    // specific value was set.
    if (param.Locked) {
      for (let preference of proxyPreferences) {
        Services.prefs.lockPref(preference);
      }
    }
  },
};
