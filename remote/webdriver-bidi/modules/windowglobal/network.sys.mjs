/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { WindowGlobalBiDiModule } from "chrome://remote/content/webdriver-bidi/modules/WindowGlobalBiDiModule.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BeforeStopRequestListener:
    "chrome://remote/content/shared/listeners/BeforeStopRequestListener.sys.mjs",
  CachedResourceListener:
    "chrome://remote/content/shared/listeners/CachedResourceListener.sys.mjs",
  DataChannelListener:
    "chrome://remote/content/shared/listeners/DataChannelListener.sys.mjs",
  NetworkRequest: "chrome://remote/content/shared/NetworkRequest.sys.mjs",
  NetworkResponse: "chrome://remote/content/shared/NetworkResponse.sys.mjs",
});

class NetworkModule extends WindowGlobalBiDiModule {
  #beforeStopRequestListener;
  #cachedResourceListener;
  #dataChannelListener;
  #subscribedEvents;

  constructor(messageHandler) {
    super(messageHandler);

    this.#beforeStopRequestListener = new lazy.BeforeStopRequestListener(
      this.messageHandler.context
    );
    this.#beforeStopRequestListener.on(
      "beforeStopRequest",
      this.#onBeforeStopRequest
    );

    this.#cachedResourceListener = new lazy.CachedResourceListener(
      this.messageHandler.context
    );
    this.#cachedResourceListener.on(
      "cached-resource-sent",
      this.#onCachedResourceSent
    );

    this.#dataChannelListener = new lazy.DataChannelListener(
      this.messageHandler.context
    );
    this.#dataChannelListener.on(
      "data-channel-opened",
      this.#onDataChannelOpened
    );

    // Set of event names which have active subscriptions.
    this.#subscribedEvents = new Set();
  }

  destroy() {
    this.#beforeStopRequestListener.destroy();
    this.#cachedResourceListener.destroy();
    this.#dataChannelListener.destroy();
    this.#subscribedEvents = null;
  }

  #emitWindowGlobalNetworkResource(channel, request, response) {
    this.messageHandler.emitEvent("network._windowGlobalNetworkResource", {
      channelId: channel.channelId,
      context: this.messageHandler.context,
      request: request.toJSON(),
      response: response.toJSON(),
    });
  }

  #onBeforeStopRequest = (event, data) => {
    this.messageHandler.emitEvent("network._beforeStopRequest", {
      channelId: data.channel.channelId,
      contextId: this.messageHandler.contextId,
      decodedBodySize: data.decodedBodySize,
    });
  };

  #onCachedResourceSent = (event, data) => {
    const request = new lazy.NetworkRequest(data.channel, {
      eventRecord: this,
      navigationManager: null,
    });
    const response = new lazy.NetworkResponse(data.channel, {
      fromCache: true,
      fromServiceWorker: false,
      isCachedResource: true,
    });

    this.#emitWindowGlobalNetworkResource(data.channel, request, response);
  };

  #onDataChannelOpened = (event, data) => {
    const request = new lazy.NetworkRequest(data.channel, {
      eventRecord: this,
      navigationManager: null,
    });
    const response = new lazy.NetworkResponse(data.channel, {
      fromCache: false,
      fromServiceWorker: false,
      isCachedResource: false,
    });

    this.#emitWindowGlobalNetworkResource(data.channel, request, response);
  };

  #startListening() {
    if (this.#subscribedEvents.size == 0) {
      this.#beforeStopRequestListener.startListening();
      this.#cachedResourceListener.startListening();
      this.#dataChannelListener.startListening();
    }
  }

  #stopListening() {
    if (this.#subscribedEvents.size == 0) {
      this.#beforeStopRequestListener.stopListening();
      this.#cachedResourceListener.stopListening();
      this.#dataChannelListener.stopListening();
    }
  }

  #subscribeEvent(event) {
    switch (event) {
      case "network.beforeRequestSent":
        this.#startListening();
        this.#subscribedEvents.add("network.beforeRequestSent");
        break;
      case "network.responseStarted":
        this.#startListening();
        this.#subscribedEvents.add("network.responseStarted");
        break;
      case "network.responseCompleted":
        this.#startListening();
        this.#subscribedEvents.add("network.responseCompleted");
        break;
    }
  }

  #unsubscribeEvent(event) {
    switch (event) {
      case "network.beforeRequestSent":
        this.#subscribedEvents.delete("network.beforeRequestSent");
        break;
      case "network.responseStarted":
        this.#subscribedEvents.delete("network.responseStarted");
        break;
      case "network.responseCompleted":
        this.#subscribedEvents.delete("network.responseCompleted");
        break;
    }

    this.#stopListening();
  }

  /**
   * Internal commands
   */

  _applySessionData(params) {
    // TODO: Bug 1775231. Move this logic to a shared module or an abstract
    // class.
    const { category } = params;
    if (category === "event") {
      const filteredSessionData = params.sessionData.filter(item =>
        this.messageHandler.matchesContext(item.contextDescriptor)
      );
      for (const event of this.#subscribedEvents.values()) {
        const hasSessionItem = filteredSessionData.some(
          item => item.value === event
        );
        // If there are no session items for this context, we should unsubscribe from the event.
        if (!hasSessionItem) {
          this.#unsubscribeEvent(event);
        }
      }

      // Subscribe to all events, which have an item in SessionData.
      for (const { value } of filteredSessionData) {
        this.#subscribeEvent(value);
      }
    }
  }
}

export const network = NetworkModule;
