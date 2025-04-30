/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  ExtensionParent: "resource://gre/modules/ExtensionParent.sys.mjs",
  ProxyService: {
    service: "@mozilla.org/network/protocol-proxy-service;1",
    iid: Ci.nsIProtocolProxyService,
  },
  getCookieStoreIdForOriginAttributes: () =>
    lazy.ExtensionParent.apiManager.global.getCookieStoreIdForOriginAttributes,
  /** @returns {TabTrackerBase} */
  tabTracker: () => lazy.ExtensionParent.apiManager.global.tabTracker,
});

// DNS is resolved on the SOCKS proxy server.
const { TRANSPARENT_PROXY_RESOLVES_HOST } = Ci.nsIProxyInfo;

// The length of time (seconds) to wait for a proxy to resolve before ignoring it.
const PROXY_TIMEOUT_SEC = 10;

const { ExtensionError } = ExtensionUtils;

const PROXY_TYPES = Object.freeze({
  DIRECT: "direct",
  HTTPS: "https",
  PROXY: "http", // Synonym for PROXY_TYPES.HTTP
  HTTP: "http",
  SOCKS: "socks", // SOCKS5
  SOCKS4: "socks4",
});

const ProxyInfoData = {
  validate(proxyData) {
    if (proxyData.type && proxyData.type.toLowerCase() === "direct") {
      return { type: proxyData.type };
    }
    for (let prop of [
      "type",
      "host",
      "port",
      "username",
      "password",
      "proxyDNS",
      "failoverTimeout",
      "proxyAuthorizationHeader",
      "connectionIsolationKey",
    ]) {
      this[prop](proxyData);
    }
    return proxyData;
  },

  type(proxyData) {
    let { type } = proxyData;
    if (
      typeof type !== "string" ||
      !PROXY_TYPES.hasOwnProperty(type.toUpperCase())
    ) {
      throw new ExtensionError(
        `ProxyInfoData: Invalid proxy server type: "${type}"`
      );
    }
    proxyData.type = PROXY_TYPES[type.toUpperCase()];
  },

  host(proxyData) {
    let { host } = proxyData;
    if (typeof host !== "string" || host.includes(" ")) {
      throw new ExtensionError(
        `ProxyInfoData: Invalid proxy server host: "${host}"`
      );
    }
    if (!host.length) {
      throw new ExtensionError(
        "ProxyInfoData: Proxy server host cannot be empty"
      );
    }
    proxyData.host = host;
  },

  port(proxyData) {
    let port = Number.parseInt(proxyData.port, 10);
    if (!Number.isInteger(port)) {
      throw new ExtensionError(
        `ProxyInfoData: Invalid proxy server port: "${port}"`
      );
    }

    if (port < 1 || port > 0xffff) {
      throw new ExtensionError(
        `ProxyInfoData: Proxy server port ${port} outside range 1 to 65535`
      );
    }
    proxyData.port = port;
  },

  username(proxyData) {
    let { username } = proxyData;
    if (username !== undefined && typeof username !== "string") {
      throw new ExtensionError(
        `ProxyInfoData: Invalid proxy server username: "${username}"`
      );
    }
  },

  password(proxyData) {
    let { password } = proxyData;
    if (password !== undefined && typeof password !== "string") {
      throw new ExtensionError(
        `ProxyInfoData: Invalid proxy server password: "${password}"`
      );
    }
  },

  proxyDNS(proxyData) {
    let { proxyDNS, type } = proxyData;
    if (proxyDNS !== undefined) {
      if (typeof proxyDNS !== "boolean") {
        throw new ExtensionError(
          `ProxyInfoData: Invalid proxyDNS value: "${proxyDNS}"`
        );
      }
      if (
        proxyDNS &&
        type !== PROXY_TYPES.SOCKS &&
        type !== PROXY_TYPES.SOCKS4
      ) {
        throw new ExtensionError(
          `ProxyInfoData: proxyDNS can only be true for SOCKS proxy servers`
        );
      }
    }
  },

  failoverTimeout(proxyData) {
    let { failoverTimeout } = proxyData;
    if (
      failoverTimeout !== undefined &&
      (!Number.isInteger(failoverTimeout) || failoverTimeout < 1)
    ) {
      throw new ExtensionError(
        `ProxyInfoData: Invalid failover timeout: "${failoverTimeout}"`
      );
    }
  },

  proxyAuthorizationHeader(proxyData) {
    let { proxyAuthorizationHeader, type } = proxyData;
    if (proxyAuthorizationHeader === undefined) {
      return;
    }
    if (typeof proxyAuthorizationHeader !== "string") {
      throw new ExtensionError(
        `ProxyInfoData: Invalid proxy server authorization header: "${proxyAuthorizationHeader}"`
      );
    }
    if (type !== "https" && type !== "http") {
      throw new ExtensionError(
        `ProxyInfoData: ProxyAuthorizationHeader requires type "https" or "http"`
      );
    }
  },

  connectionIsolationKey(proxyData) {
    let { connectionIsolationKey } = proxyData;
    if (
      connectionIsolationKey !== undefined &&
      typeof connectionIsolationKey !== "string"
    ) {
      throw new ExtensionError(
        `ProxyInfoData: Invalid proxy connection isolation key: "${connectionIsolationKey}"`
      );
    }
  },

  createProxyInfoFromData(
    policy,
    proxyDataList,
    defaultProxyInfo,
    proxyDataListIndex = 0
  ) {
    if (proxyDataListIndex >= proxyDataList.length) {
      return defaultProxyInfo;
    }
    let proxyData = proxyDataList[proxyDataListIndex];
    if (proxyData == null) {
      return null;
    }
    let {
      type,
      host,
      port,
      username,
      password,
      proxyDNS,
      failoverTimeout,
      proxyAuthorizationHeader,
      connectionIsolationKey,
    } = ProxyInfoData.validate(proxyData);
    if (type === PROXY_TYPES.DIRECT && defaultProxyInfo) {
      return defaultProxyInfo;
    }
    let failoverProxy = this.createProxyInfoFromData(
      policy,
      proxyDataList,
      defaultProxyInfo,
      proxyDataListIndex + 1
    );

    let proxyInfo;
    if (type === PROXY_TYPES.SOCKS || type === PROXY_TYPES.SOCKS4) {
      proxyInfo = lazy.ProxyService.newProxyInfoWithAuth(
        type,
        host,
        port,
        username,
        password,
        proxyAuthorizationHeader,
        connectionIsolationKey,
        proxyDNS ? TRANSPARENT_PROXY_RESOLVES_HOST : 0,
        failoverTimeout ? failoverTimeout : PROXY_TIMEOUT_SEC,
        failoverProxy
      );
    } else {
      proxyInfo = lazy.ProxyService.newProxyInfo(
        type,
        host,
        port,
        proxyAuthorizationHeader,
        connectionIsolationKey,
        proxyDNS ? TRANSPARENT_PROXY_RESOLVES_HOST : 0,
        failoverTimeout ? failoverTimeout : PROXY_TIMEOUT_SEC,
        failoverProxy
      );
    }
    proxyInfo.sourceId = policy.id;
    return proxyInfo;
  },
};

function normalizeFilter(filter) {
  if (!filter) {
    filter = {};
  }

  return {
    urls: filter.urls || null,
    types: filter.types || null,
    tabId: filter.tabId ?? null,
    windowId: filter.windowId ?? null,
    incognito: filter.incognito ?? null,
  };
}

export class ProxyChannelFilter {
  constructor(context, extension, listener, filter, extraInfoSpec) {
    this.context = context;
    this.extension = extension;
    this.filter = normalizeFilter(filter);
    this.listener = listener;
    this.extraInfoSpec = extraInfoSpec || [];

    lazy.ProxyService.registerChannelFilter(
      this /* nsIProtocolProxyChannelFilter aFilter */,
      0 /* unsigned long aPosition */
    );
  }

  // Originally duplicated from WebRequest.sys.mjs with small changes.  Keep this
  // in sync with WebRequest.sys.mjs as well as parent/ext-webRequest.js when
  // apropiate.
  getRequestData(channel, extraData) {
    let originAttributes = channel.loadInfo?.originAttributes;
    let data = {
      requestId: String(channel.id),
      url: channel.finalURL,
      method: channel.method,
      type: channel.type,
      fromCache: !!channel.fromCache,
      incognito: originAttributes?.privateBrowsingId > 0,
      thirdParty: channel.thirdParty,

      originUrl: channel.originURL || undefined,
      documentUrl: channel.documentURL || undefined,

      frameId: channel.frameId,
      parentFrameId: channel.parentFrameId,

      frameAncestors: channel.frameAncestors || undefined,

      timeStamp: Date.now(),

      ...extraData,
    };
    if (originAttributes) {
      data.cookieStoreId =
        lazy.getCookieStoreIdForOriginAttributes(originAttributes);
    }
    if (this.extraInfoSpec.includes("requestHeaders")) {
      data.requestHeaders = channel.getRequestHeaders();
    }
    if (channel.urlClassification) {
      data.urlClassification = {
        firstParty: channel.urlClassification.firstParty.filter(
          c => !c.startsWith("socialtracking")
        ),
        thirdParty: channel.urlClassification.thirdParty.filter(
          c => !c.startsWith("socialtracking")
        ),
      };
    }
    return data;
  }

  /**
   * This method (which is required by the nsIProtocolProxyService interface)
   * is called to apply proxy filter rules for the given URI and proxy object
   * (or list of proxy objects).
   *
   * @param {nsIChannel} channel The channel for which these proxy settings apply.
   * @param {nsIProxyInfo} defaultProxyInfo The proxy (or list of proxies) that
   *     would be used by default for the given URI. This may be null.
   * @param {nsIProxyProtocolFilterResult} proxyFilter
   */
  async applyFilter(channel, defaultProxyInfo, proxyFilter) {
    let proxyInfo;
    try {
      let wrapper = ChannelWrapper.get(channel);

      let browserData = { tabId: -1, windowId: -1 };
      if (wrapper.browserElement) {
        browserData = lazy.tabTracker.getBrowserData(wrapper.browserElement);
      }

      let { filter, extension } = this;
      if (filter.tabId != null && browserData.tabId !== filter.tabId) {
        return;
      }
      if (filter.windowId != null && browserData.windowId !== filter.windowId) {
        return;
      }
      if (
        extension.userContextIsolation &&
        !extension.canAccessContainer(
          channel.loadInfo?.originAttributes.userContextId
        )
      ) {
        return;
      }

      let { policy } = this.extension;
      if (wrapper.matches(filter, policy, { isProxy: true })) {
        let data = this.getRequestData(wrapper, { tabId: browserData.tabId });

        let ret = await this.listener(data);
        if (ret == null) {
          // If ret undefined or null, fall through to the `finally` block to apply the proxy result.
          proxyInfo = ret;
          return;
        }
        // We only accept proxyInfo objects, not the PAC strings. ProxyInfoData will
        // accept either, so we want to enforce the limit here.
        if (typeof ret !== "object") {
          throw new ExtensionError(
            "ProxyInfoData: proxyData must be an object or array of objects"
          );
        }
        // We allow the call to return either a single proxyInfo or an array of proxyInfo.
        if (!Array.isArray(ret)) {
          ret = [ret];
        }
        proxyInfo = ProxyInfoData.createProxyInfoFromData(
          policy,
          ret,
          defaultProxyInfo
        );
      }
    } catch (e) {
      // We need to normalize errors to dispatch them to the extension handler.  If
      // we have not started up yet, we'll just log those to the console.
      if (!this.context) {
        this.extension.logError(`proxy-error before extension startup: ${e}`);
        return;
      }
      let error = this.context.normalizeError(e);
      this.extension.emit("proxy-error", {
        message: error.message,
        fileName: error.fileName,
        lineNumber: error.lineNumber,
        stack: error.stack,
      });
    } finally {
      // We must call onProxyFilterResult.  proxyInfo may be null or nsIProxyInfo.
      // defaultProxyInfo will be null unless a prior proxy handler has set something.
      // If proxyInfo is null, that removes any prior proxy config.  This allows a
      // proxy extension to override higher level (e.g. prefs) config under certain
      // circumstances.
      proxyFilter.onProxyFilterResult(
        proxyInfo !== undefined ? proxyInfo : defaultProxyInfo
      );
    }
  }

  destroy() {
    lazy.ProxyService.unregisterFilter(this);
  }
}
