/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const WebSocketWatcher = require("resource://devtools/server/actors/resources/websockets.js");

add_task(function test_frame_received_before_websocket_opened() {
  const watcher = new WebSocketWatcher();
  watcher.targetActor = {
    conn: null,
    manage() {},
  };

  let availableResources;
  watcher.onAvailable = resources => {
    availableResources = resources;
  };

  const httpChannelId = 123456;
  watcher.frameReceived(1, httpChannelId, {
    payload: "early server frame",
    timeStamp: 12.5,
    finBit: true,
    rsvBit1: false,
    rsvBit2: false,
    rsvBit3: false,
    opCode: Ci.nsIWebSocketFrame.OPCODE_TEXT,
    mask: 0,
    maskBit: false,
  });

  Assert.deepEqual(availableResources, [
    {
      wsMessageType: "frameReceived",
      httpChannelId,
      data: {
        type: "received",
        payload: "early server frame",
        payloadSize: 18,
        timeStamp: 12.5,
        finBit: true,
        rsvBit1: false,
        rsvBit2: false,
        rsvBit3: false,
        opCode: Ci.nsIWebSocketFrame.OPCODE_TEXT,
        mask: 0,
        maskBit: false,
      },
    },
  ]);
});

add_task(function test_frame_payload_encoding() {
  const watcher = new WebSocketWatcher();
  const actors = [];
  watcher.targetActor = {
    conn: null,
    manage(actor) {
      actors.push(actor);
    },
  };

  let resource;
  watcher.onAvailable = resources => {
    [resource] = resources;
  };

  const { OPCODE_TEXT, OPCODE_BINARY, OPCODE_PING } = Ci.nsIWebSocketFrame;
  const cases = [
    { payload: "\xe2\x99\xa5", opCode: OPCODE_TEXT, expected: "\u2665" },
    { payload: "ASCII", opCode: OPCODE_TEXT, expected: "ASCII" },
    { payload: "", opCode: OPCODE_TEXT, expected: "" },
    { payload: "\xef\xbb\xbfa", opCode: OPCODE_TEXT, expected: "\ufeffa" },
    {
      payload: "caf\xc3\xa9 \xf0\x9f\x8c\x8d",
      opCode: OPCODE_TEXT,
      expected: "caf\u00e9 \u{1f30d}",
    },
    {
      payload: "\xe2\x99\xa5\x00\xff",
      opCode: OPCODE_BINARY,
      expected: "\xe2\x99\xa5\x00\xff",
    },
    {
      payload: "\xe2\x99\xa5",
      opCode: OPCODE_PING,
      expected: "\xe2\x99\xa5",
    },
    {
      payload: "\xe2\x99\xa5".repeat(DevToolsServer.LONG_STRING_LENGTH),
      opCode: OPCODE_TEXT,
      expected: "\u2665".repeat(DevToolsServer.LONG_STRING_LENGTH),
    },
  ];

  for (const method of ["frameSent", "frameReceived"]) {
    for (const { payload, opCode, expected } of cases) {
      watcher[method](1, 123456, {
        payload,
        opCode,
        timeStamp: 12.5,
        finBit: true,
        rsvBit1: false,
        rsvBit2: false,
        rsvBit3: false,
        mask: 0,
        maskBit: false,
      });

      const actor = actors.at(-1);
      Assert.equal(actor.substring(0), expected, method);
      Assert.equal(resource.data.payloadSize, payload.length, "Byte length");
      if (typeof resource.data.payload === "string") {
        Assert.equal(resource.data.payload, expected, "Decoded short payload");
      } else {
        Assert.equal(resource.data.payload.length, expected.length);
        Assert.equal(
          resource.data.payload.initial,
          expected.substring(0, DevToolsServer.LONG_STRING_INITIAL_LENGTH),
          "Decoded long string preview"
        );
      }
    }
  }
});
