/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

const PREF_ISOLATE_BY_USER_CONTEXT = "permissions.isolateBy.userContext";

add_setup(async function flip_userContext_isolation() {
  const flipped = !Services.prefs.getBoolPref(PREF_ISOLATE_BY_USER_CONTEXT);
  info(`Running with ${PREF_ISOLATE_BY_USER_CONTEXT}=${flipped}`);
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_ISOLATE_BY_USER_CONTEXT, flipped]],
  });
});

Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "browser_containers_permissions.js",
  this
);
