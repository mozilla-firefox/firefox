/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var {
  BULK_REQUEST,
  BULK_RESPONSE,
} = require("resource://devtools/shared/protocol/types.js");
var { Front } = require("resource://devtools/shared/protocol/Front.js");

const logger = console.createInstance({
  logModule: "devtools_rdp",
  maxLogLevel: "Warn",
});

// Hack MOZ_LOG/Console.cpp usage of ToSource logic
// to be able to write raw strings to stdout.
// This prevents being wrapped with quotes, and allow to use ANSI color codes.
const SEND_MOZ_LOG_SYMBOL = {
  toSource() {
    return " \x1b[2m->\x1b[0m ";
  },
};
let colorCounter = 0;

/**
 * Generates request methods as described by the given actor specification on
 * the given front prototype. Returns the front prototype.
 */
var generateRequestMethods = function (actorSpec, frontProto) {
  if (frontProto._actorSpec) {
    throw new Error("frontProto called twice on the same front prototype!");
  }

  frontProto.typeName = actorSpec.typeName;

  // Generate request methods.
  const methods = actorSpec.methods;
  methods.forEach(spec => {
    const { name } = spec;

    frontProto[name] = function (...args) {
      // If the front is destroyed, the request will not be able to complete.
      if (this.isDestroyed()) {
        throw new Error(
          `Can not send request '${name}' because front '${this.typeName}' is already destroyed.`
        );
      }

      const startTime = ChromeUtils.now();
      let packet;
      try {
        packet = spec.request.write(args, this);
      } catch (ex) {
        console.error("Error writing request: " + name);
        throw ex;
      }
      if (spec.oneway) {
        // Log outgoing RDP packet being sent via Protocol.js
        // (packet sent via DevToolsClient will be logged from DevToolsClient codebase)
        logger.log(SEND_MOZ_LOG_SYMBOL, packet);

        // Fire-and-forget oneway packets.
        this.send(packet);
        return undefined;
      }

      // Check if the client request should be sent as a bulk request
      const isSendingBulkData = spec.request.template === BULK_REQUEST;

      // If so, pass the last front argument as the bulk initialization callback
      const clientBulkCallback = isSendingBulkData ? args.at(-1) : null;

      // For each incoming request, we will rotate through the first 15 existing ANSI colors
      // which are all quite visible and different from each others.
      // Note that colors are specific to each request and not each front.
      let color;
      if (logger.shouldLog("Log")) {
        color = 1 + (colorCounter % 15);
        colorCounter++;
        logger.log(
          {
            toSource() {
              return `\x1b[38;5;${color}m->\x1b[0m`;
            },
          },
          // Ensure adding the `to` attribute which will be later set by request method.
          { ...packet, to: this.actorID }
        );
      }

      return this.request(packet, {
        bulk: isSendingBulkData,
        clientBulkCallback,
      }).then(response => {
        // If the request returns bulk data, return the transport response as-is.
        // We do not expect any custom packet/attributes for bulk responses,
        // the transport will handle the binary stream communication and expose
        // the StreamCopier as resolution value in the returned Promise.
        const isReceivingBulkData = spec.response.template === BULK_RESPONSE;
        if (isReceivingBulkData) {
          return response;
        }
        if (logger.shouldLog("Log")) {
          logger.log(
            {
              toSource() {
                return `\x1b[38;5;${color}m<-\x1b[0m`;
              },
            },
            response
          );
        }

        let ret;
        if (!this.conn) {
          throw new Error("Missing conn on " + this);
        }
        if (this.isDestroyed()) {
          throw new Error(
            `Can not interpret '${name}' response because front '${this.typeName}' is already destroyed.`
          );
        }
        try {
          ret = spec.response.read(response, this);
        } catch (ex) {
          console.error("Error reading response to: " + name + "\n" + ex);
          throw ex;
        }
        ChromeUtils.addProfilerMarker(
          "RDP Front",
          startTime,
          `${this.typeName}:${name}()`
        );
        return ret;
      });
    };

    // Release methods should call the destroy function on return.
    if (spec.release) {
      const fn = frontProto[name];
      frontProto[name] = async function (...args) {
        // Note that if release is oneway, fn.apply won't be async,
        // nor return any value.
        const result = await fn.apply(this, args);
        this.destroy();
        return result;
      };
    }
  });

  // Process event specifications
  frontProto._clientSpec = {};

  const actorEvents = actorSpec.events;
  if (actorEvents) {
    frontProto._clientSpec.events = new Map();

    for (const [name, request] of actorEvents) {
      frontProto._clientSpec.events.set(request.type, {
        name,
        request,
      });
    }
  }

  frontProto._actorSpec = actorSpec;

  return frontProto;
};

/**
 * Create a front class for the given actor specification and front prototype.
 *
 * @param object actorSpec
 *    The actor specification you're creating a front for.
 * @param object proto
 *    The object prototype.  Must have a 'typeName' property,
 *    should have method definitions, can have event definitions.
 */
var FrontClassWithSpec = function (actorSpec) {
  class OneFront extends Front {}
  generateRequestMethods(actorSpec, OneFront.prototype);
  return OneFront;
};
exports.FrontClassWithSpec = FrontClassWithSpec;
