/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { Actor } = require("resource://devtools/shared/protocol.js");
const {
  threadConfigurationSpec,
} = require("resource://devtools/shared/specs/thread-configuration.js");

const { SessionDataHelpers } = ChromeUtils.importESModule(
  "resource://devtools/server/actors/watcher/SessionDataHelpers.sys.mjs",
  { global: "contextual" }
);
const {
  SUPPORTED_DATA: { THREAD_CONFIGURATION },
} = SessionDataHelpers;

// List of options supported by this thread configuration actor.
/* eslint sort-keys: "error" */
const SUPPORTED_OPTIONS = {
  // Disable pausing on caught exceptions.
  ignoreCaughtExceptions: true,
  // Log the event break points.
  logEventBreakpoints: true,
  // @backward-compat { version 154 } asm.js was removed in Fx154; accepted
  // from old clients for compatibility but no longer acted upon.
  observeAsmJS: true,
  // Enable debugging wasm.
  observeWasm: true,
  // Enable pausing on exceptions.
  pauseOnExceptions: true,
  // Boolean to know if we should display the overlay when pausing
  pauseOverlay: true,
  // Should pause all the workers untill thread has attached.
  pauseWorkersUntilAttach: true,
  // Include async stack frames when paused.
  shouldIncludeAsyncLiveFrames: true,
  // Include previously saved stack frames when paused.
  shouldIncludeSavedFrames: true,
  // Controls pausing on debugger statement.
  // (This is enabled by default if omitted)
  shouldPauseOnDebuggerStatement: true,
  // Stop pausing on breakpoints.
  skipBreakpoints: true,
};
/* eslint-disable sort-keys */

/**
 * This actor manages the configuration options which apply to thread actor for all the targets.
 *
 * Configuration options should be applied to all concerned targets when the
 * configuration is updated, and new targets should also be able to read the
 * flags when they are created. The flags will be forwarded to the WatcherActor
 * and stored as THREAD_CONFIGURATION data entries.
 *
 * @class
 */
class ThreadConfigurationActor extends Actor {
  constructor(watcherActor) {
    super(watcherActor.conn, threadConfigurationSpec);
    this.watcherActor = watcherActor;
  }

  async updateConfiguration(configuration) {
    const configArray = Object.keys(configuration)
      .filter(key => {
        if (!SUPPORTED_OPTIONS[key]) {
          console.warn(`Unsupported option for ThreadConfiguration: ${key}`);
          return false;
        }
        return true;
      })
      .map(key => ({ key, value: configuration[key] }));

    await this.watcherActor.addOrSetDataEntry(
      THREAD_CONFIGURATION,
      configArray,
      "add"
    );
  }
}

exports.ThreadConfigurationActor = ThreadConfigurationActor;
