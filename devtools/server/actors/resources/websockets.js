/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  LongStringActor,
} = require("resource://devtools/server/actors/string.js");

const webSocketEventService = Cc[
  "@mozilla.org/websocketevent/service;1"
].getService(Ci.nsIWebSocketEventService);

class WebSocketWatcher {
  constructor() {
    this.windowIds = new Set();
    this.onWindowReady = this.onWindowReady.bind(this);
    this.onWindowDestroy = this.onWindowDestroy.bind(this);
  }

  static createResource(wsMessageType, eventParams) {
    return {
      wsMessageType,
      ...eventParams,
    };
  }

  static prepareFramePayload(targetActor, frame) {
    const text =
      frame.opCode === Ci.nsIWebSocketFrame.OPCODE_TEXT
        ? new TextDecoder("utf-8", { ignoreBOM: true }).decode(
            Uint8Array.from(frame.payload, byte => byte.charCodeAt(0))
          )
        : frame.payload;
    const payload = new LongStringActor(targetActor.conn, text);
    targetActor.manage(payload);
    return payload.form();
  }

  watch(targetActor, { onAvailable }) {
    this.targetActor = targetActor;
    this.onAvailable = onAvailable;

    for (const window of this.targetActor.windows) {
      const { innerWindowId } = window.windowGlobalChild;
      this.startListening(innerWindowId);
    }

    // On navigate/reload we should re-start listening with the
    // new `innerWindowID`
    this.targetActor.on("window-ready", this.onWindowReady);
    this.targetActor.on("window-destroyed", this.onWindowDestroy);
  }

  onWindowReady({ window }) {
    if (!this.targetActor.followWindowGlobalLifeCycle) {
      const { innerWindowId } = window.windowGlobalChild;
      this.startListening(innerWindowId);
    }
  }

  onWindowDestroy({ id }) {
    this.stopListening(id);
  }

  startListening(innerWindowId) {
    if (!this.windowIds.has(innerWindowId)) {
      this.windowIds.add(innerWindowId);
      webSocketEventService.addListener(innerWindowId, this);
    }
  }

  stopListening(innerWindowId) {
    if (this.windowIds.has(innerWindowId)) {
      this.windowIds.delete(innerWindowId);
      if (!webSocketEventService.hasListenerFor(innerWindowId)) {
        // The listener might have already been cleaned up on `window-destroy`.
        console.warn(
          "Already stopped listening to websocket events for this window."
        );
        return;
      }
      webSocketEventService.removeListener(innerWindowId, this);
    }
  }

  destroy() {
    for (const id of this.windowIds) {
      this.stopListening(id);
    }
    this.targetActor.off("window-ready", this.onWindowReady);
    this.targetActor.off("window-destroyed", this.onWindowDestroy);
  }

  // methods for the nsIWebSocketEventService
  webSocketCreated() {}

  webSocketOpened(
    _webSocketSerialID,
    effectiveURI,
    protocols,
    extensions,
    httpChannelId
  ) {
    const resource = WebSocketWatcher.createResource("webSocketOpened", {
      httpChannelId,
      effectiveURI,
      protocols,
      extensions,
    });

    this.onAvailable([resource]);
  }

  webSocketMessageAvailable() {}

  webSocketClosed(_webSocketSerialID, httpChannelId, wasClean, code, reason) {
    const resource = WebSocketWatcher.createResource("webSocketClosed", {
      httpChannelId,
      wasClean,
      code,
      reason,
    });

    this.onAvailable([resource]);
  }

  frameReceived(_webSocketSerialID, httpChannelId, frame) {
    const payload = WebSocketWatcher.prepareFramePayload(
      this.targetActor,
      frame
    );
    const resource = WebSocketWatcher.createResource("frameReceived", {
      httpChannelId,
      data: {
        type: "received",
        payload,
        payloadSize: frame.payload.length,
        timeStamp: frame.timeStamp,
        finBit: frame.finBit,
        rsvBit1: frame.rsvBit1,
        rsvBit2: frame.rsvBit2,
        rsvBit3: frame.rsvBit3,
        opCode: frame.opCode,
        mask: frame.mask,
        maskBit: frame.maskBit,
      },
    });

    this.onAvailable([resource]);
  }

  frameSent(_webSocketSerialID, httpChannelId, frame) {
    const payload = WebSocketWatcher.prepareFramePayload(
      this.targetActor,
      frame
    );
    const resource = WebSocketWatcher.createResource("frameSent", {
      httpChannelId,
      data: {
        type: "sent",
        payload,
        payloadSize: frame.payload.length,
        timeStamp: frame.timeStamp,
        finBit: frame.finBit,
        rsvBit1: frame.rsvBit1,
        rsvBit2: frame.rsvBit2,
        rsvBit3: frame.rsvBit3,
        opCode: frame.opCode,
        mask: frame.mask,
        maskBit: frame.maskBit,
      },
    });

    this.onAvailable([resource]);
  }
}

module.exports = WebSocketWatcher;
