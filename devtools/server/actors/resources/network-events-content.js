/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

loader.lazyRequireGetter(
  this,
  "NetworkEventActor",
  "resource://devtools/server/actors/network-monitor/network-event-actor.js",
  true
);

const lazy = {};

ChromeUtils.defineESModuleGetters(
  lazy,
  {
    NetworkUtils:
      "resource://devtools/shared/network-observer/NetworkUtils.sys.mjs",
  },
  { global: "contextual" }
);

// Internal resource types used to create the appropriate network event based
// on where the channel / resource is coming from.
const RESOURCE_TYPES = {
  BLOCKED: "blocked-resource",
  CACHED: "cached-resource",
  DATA_CHANNEL: "data-channel-resource",
};

/**
 * Handles network events from the content process
 * This currently only handles events for requests (js/css) blocked by CSP.
 */
class NetworkEventContentWatcher {
  /**
   * Start watching for all network events related to a given Target Actor.
   *
   * @param TargetActor targetActor
   *        The target actor in the content process from which we should
   *        observe network events.
   * @param Object options
   *        Dictionary object with following attributes:
   *        - onAvailable: mandatory function
   *          This will be called for each resource.
   *        - onUpdated: optional function
   *          This would be called multiple times for each resource.
   */
  async watch(targetActor, { onAvailable, onUpdated }) {
    // Map from channelId to network event objects.
    this.networkEvents = new Map();

    this.targetActor = targetActor;
    this.onAvailable = onAvailable;
    this.onUpdated = onUpdated;

    Services.obs.addObserver(
      this.httpFailedOpeningRequest,
      "http-on-failed-opening-request"
    );

    Services.obs.addObserver(
      this.httpOnResourceCacheResponse,
      "http-on-resource-cache-response"
    );

    Services.obs.addObserver(this.onDataChannelOpened, "data-channel-opened");
  }
  /**
   * Allows clearing of network events
   */
  clear() {
    this.networkEvents.clear();
  }

  httpFailedOpeningRequest = subject => {
    const channel = subject.QueryInterface(Ci.nsIHttpChannel);

    // Ignore preload requests to avoid duplicity request entries in
    // the Network panel. If a preload fails (for whatever reason)
    // then the platform kicks off another 'real' request.
    if (lazy.NetworkUtils.isPreloadRequest(channel)) {
      return;
    }

    if (
      !lazy.NetworkUtils.matchRequest(channel, {
        targetActor: this.targetActor,
      })
    ) {
      return;
    }

    this.onNetworkEventAvailable(channel, {
      networkEventOptions: {
        blockedReason: channel.loadInfo.requestBlockingReason,
      },
      type: RESOURCE_TYPES.BLOCKED,
    });
  };

  httpOnResourceCacheResponse = (subject, topic) => {
    if (
      topic != "http-on-resource-cache-response" ||
      !(subject instanceof Ci.nsIHttpChannel)
    ) {
      return;
    }

    const channel = subject.QueryInterface(Ci.nsIHttpChannel);

    if (
      !lazy.NetworkUtils.matchRequest(channel, {
        targetActor: this.targetActor,
      })
    ) {
      return;
    }

    if (
      channel.loadInfo?.externalContentPolicyType !==
      Ci.nsIContentPolicy.TYPE_SCRIPT
    ) {
      // For images and stylesheets from the cache, only one network request
      // should be created per URI.
      //
      // For scripts from the cache, multiple network requests should be
      // created.
      const hasURI = Array.from(this.networkEvents.values()).some(
        networkEvent => networkEvent.uri === channel.URI.spec
      );

      if (hasURI) {
        return;
      }
    }

    this.onNetworkEventAvailable(channel, {
      fromCache: true,
      networkEventOptions: {},
      type: RESOURCE_TYPES.CACHED,
    });
  };

  onDataChannelOpened = (subject, topic) => {
    if (
      topic != "data-channel-opened" ||
      !(subject instanceof Ci.nsIDataChannel)
    ) {
      return;
    }

    const channel = subject.QueryInterface(Ci.nsIDataChannel);
    channel.QueryInterface(Ci.nsIIdentChannel);
    channel.QueryInterface(Ci.nsIChannel);

    if (channel.isDocument) {
      // Navigation data channels are available in the parent process and will
      // be monitored there.
      return;
    }

    if (
      !lazy.NetworkUtils.matchRequest(channel, {
        targetActor: this.targetActor,
      })
    ) {
      return;
    }

    this.onNetworkEventAvailable(channel, {
      fromCache: false,
      networkEventOptions: {},
      type: RESOURCE_TYPES.DATA_CHANNEL,
    });
  };

  onNetworkEventAvailable(channel, { fromCache, networkEventOptions, type }) {
    const networkEventActor = new NetworkEventActor(
      this.targetActor.conn,
      this.targetActor.sessionContext,
      {
        onNetworkEventUpdate: this.onNetworkEventUpdate.bind(this),
        onNetworkEventDestroy: this.onNetworkEventDestroyed.bind(this),
      },
      networkEventOptions,
      channel
    );
    this.targetActor.manage(networkEventActor);

    const resource = networkEventActor.asResource();

    const networkEvent = {
      browsingContextID: resource.browsingContextID,
      innerWindowId: resource.innerWindowId,
      resourceId: resource.resourceId,
      receivedUpdates: [],
      resourceUpdates: {
        // Requests already come with request cookies and headers, so those
        // should always be considered as available. But the client still
        // heavily relies on those `Available` flags to fetch additional data,
        // so it is better to keep them for consistency.
        requestCookiesAvailable: true,
        requestHeadersAvailable: true,
      },
      uri: channel.URI.spec,
    };
    this.networkEvents.set(resource.resourceId, networkEvent);

    this.onAvailable([resource]);

    networkEventActor.addCacheDetails({ fromCache });
    if (type == RESOURCE_TYPES.BLOCKED) {
      this._emitUpdate(networkEvent);
    } else if (type == RESOURCE_TYPES.CACHED) {
      networkEventActor.addResponseStart({ channel, fromCache: true });
      networkEventActor.addEventTimings(
        0 /* totalTime */,
        {} /* timings */,
        {} /* offsets */
      );
      networkEventActor.addServerTimings({});
      networkEventActor.addResponseContent(
        {
          mimeType: channel.contentType,
          size: channel.contentLength,
          text: "",
          transferredSize: 0,
        },
        {}
      );
    } else if (type == RESOURCE_TYPES.DATA_CHANNEL) {
      lazy.NetworkUtils.handleDataChannel(channel, networkEventActor);
    }
  }

  onNetworkEventUpdate(updateResource) {
    const networkEvent = this.networkEvents.get(updateResource.resourceId);

    if (!networkEvent) {
      return;
    }

    const { resourceUpdates, receivedUpdates } = networkEvent;

    switch (updateResource.updateType) {
      case "cacheDetails":
        resourceUpdates.fromCache = updateResource.fromCache;
        resourceUpdates.fromServiceWorker = updateResource.fromServiceWorker;
        break;
      case "responseStart":
        // For cached image requests channel.responseStatus is set to 200 as
        // expected. However responseStatusText is empty. In this case fallback
        // to the expected statusText "OK".
        let statusText = updateResource.statusText;
        if (!statusText && updateResource.status === "200") {
          statusText = "OK";
        }
        resourceUpdates.httpVersion = updateResource.httpVersion;
        resourceUpdates.status = updateResource.status;
        resourceUpdates.statusText = statusText;
        resourceUpdates.remoteAddress = updateResource.remoteAddress;
        resourceUpdates.remotePort = updateResource.remotePort;
        resourceUpdates.waitingTime = updateResource.waitingTime;

        resourceUpdates.responseHeadersAvailable = true;
        resourceUpdates.responseCookiesAvailable = true;
        break;
      case "responseContent":
        resourceUpdates.contentSize = updateResource.contentSize;
        resourceUpdates.mimeType = updateResource.mimeType;
        resourceUpdates.transferredSize = updateResource.transferredSize;
        break;
      case "eventTimings":
        resourceUpdates.totalTime = updateResource.totalTime;
        break;
    }

    resourceUpdates[`${updateResource.updateType}Available`] = true;
    receivedUpdates.push(updateResource.updateType);

    // Here we explicitly call all three `add` helpers on each network event
    // actor so in theory we could check only the last one to be called, ie
    // responseContent.
    const isComplete =
      receivedUpdates.includes("responseStart") &&
      receivedUpdates.includes("responseContent") &&
      receivedUpdates.includes("eventTimings");

    if (isComplete) {
      this._emitUpdate(networkEvent);
    }
  }

  _emitUpdate(networkEvent) {
    this.onUpdated([
      {
        resourceId: networkEvent.resourceId,
        resourceUpdates: networkEvent.resourceUpdates,
        browsingContextID: networkEvent.browsingContextID,
        innerWindowId: networkEvent.innerWindowId,
      },
    ]);
  }

  onNetworkEventDestroyed(channelId) {
    if (this.networkEvents.has(channelId)) {
      this.networkEvents.delete(channelId);
    }
  }

  destroy() {
    this.clear();
    Services.obs.removeObserver(
      this.httpFailedOpeningRequest,
      "http-on-failed-opening-request"
    );

    Services.obs.removeObserver(
      this.httpOnResourceCacheResponse,
      "http-on-resource-cache-response"
    );

    Services.obs.removeObserver(
      this.onDataChannelOpened,
      "data-channel-opened"
    );
  }
}

module.exports = NetworkEventContentWatcher;
