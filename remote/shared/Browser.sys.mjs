/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  Log: "chrome://remote/content/shared/Log.sys.mjs",
  pprint: "chrome://remote/content/shared/Format.sys.mjs",
  waitForObserverTopic: "chrome://remote/content/shared/Sync.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () => lazy.Log.get());

/**
 * Quits the application with the provided flags.
 *
 * Optional {@link nsIAppStartup} flags may be provided as
 * an array of masks, and these will be combined by ORing
 * them with a bitmask. The available masks are defined in
 * https://developer.mozilla.org/en-US/docs/Mozilla/Tech/XPCOM/Reference/Interface/nsIAppStartup.
 *
 * Crucially, only one of the *Quit flags can be specified. The |eRestart|
 * flag may be bit-wise combined with one of the *Quit flags to cause
 * the application to restart after it quits.
 *
 * @param {Array.<string>=} flags
 *     Constant name of masks to pass to |Services.startup.quit|.
 *     If empty or undefined, |nsIAppStartup.eAttemptQuit| is used.
 * @param {boolean=} safeMode
 *     Optional flag to indicate that the application has to
 *     be restarted in safe mode.
 * @param {boolean=} isWindowless
 *     Optional flag to indicate that the browser was started in windowless mode.
 *
 * @returns {Record<string, boolean>}
 *     Dictionary containing information that explains the shutdown reason.
 *     The value for `cause` contains the shutdown kind like "shutdown" or
 *     "restart", while `forced` will indicate if it was a normal or forced
 *     shutdown of the application. "in_app" is always set to indicate that
 *     it is a shutdown triggered from within the application.
 */
export async function quit(flags = [], safeMode = false, isWindowless = false) {
  if (flags.includes("eSilently")) {
    if (!isWindowless) {
      throw new Error(
        `Silent restarts only allowed with "moz:windowless" capability set`
      );
    }
    if (!flags.includes("eRestart")) {
      throw new TypeError(`"silently" only works with restart flag`);
    }
  }

  const quits = ["eConsiderQuit", "eAttemptQuit", "eForceQuit"];

  let quitSeen;
  let mode = 0;
  if (flags.length) {
    for (let k of flags) {
      if (!(k in Ci.nsIAppStartup)) {
        throw new TypeError(lazy.pprint`Expected ${k} in ${Ci.nsIAppStartup}`);
      }

      if (quits.includes(k)) {
        if (quitSeen) {
          throw new TypeError(`${k} cannot be combined with ${quitSeen}`);
        }
        quitSeen = k;
      }

      mode |= Ci.nsIAppStartup[k];
    }
  }

  if (!quitSeen) {
    mode |= Ci.nsIAppStartup.eAttemptQuit;
  }

  // Notify all windows that an application quit has been requested.
  // If the shutdown of the application is prevented force quit it instead.
  const forced = !Services.startup.canQuit();
  if (forced) {
    lazy.logger.trace(
      "Forcing shutdown because attempting to quit was prevented"
    );
    mode |= Ci.nsIAppStartup.eForceQuit;
  }

  // Delay response until the application is about to quit.
  const quitApplicationPromise = lazy.waitForObserverTopic("quit-application", {
    logging: false,
  });

  if (safeMode) {
    Services.startup.restartInSafeMode(mode);
  } else {
    Services.startup.quit(mode);
  }

  return {
    cause: (await quitApplicationPromise).data,
    forced,
    in_app: true,
  };
}
