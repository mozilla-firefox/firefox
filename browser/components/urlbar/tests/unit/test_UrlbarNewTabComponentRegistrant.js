/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests that the newtab <moz-urlbar> and the handoff search bar take turns
// registering as New Tab's search component, depending on the urlbar's newtab
// feature gate.

"use strict";

const { AboutNewTabComponentRegistry } = ChromeUtils.importESModule(
  "moz-src:///browser/components/newtab/AboutNewTabComponents.sys.mjs"
);

function searchComponentOf(registry) {
  return registry.values.find(
    config => config.type == AboutNewTabComponentRegistry.TYPES.SEARCH
  );
}

async function setFeatureGate(registry, enabled) {
  let updated = registry.once(AboutNewTabComponentRegistry.UPDATED_EVENT);
  UrlbarPrefs.set("newtab.featureGate", enabled);
  await updated;
}

add_task(async function test_featureGate() {
  let registry = new AboutNewTabComponentRegistry();
  registerCleanupFunction(() => {
    registry.destroy();
    Services.prefs.clearUserPref("browser.urlbar.newtab.featureGate");
  });

  Assert.equal(
    searchComponentOf(registry).tagName,
    "content-search-handoff-ui",
    "The handoff search bar is registered while the feature gate is disabled"
  );

  await setFeatureGate(registry, true);
  let component = searchComponentOf(registry);
  Assert.equal(
    component.tagName,
    "moz-urlbar",
    "<moz-urlbar> supersedes the handoff search bar once the gate is enabled"
  );
  Assert.equal(
    component.componentURL,
    "chrome://browser/content/urlbar/UrlbarInput.mjs",
    "<moz-urlbar> is delivered from the module that defines it"
  );
  Assert.equal(
    component.attributes["sap-name"],
    "newtab_searchbar",
    "<moz-urlbar> reports the newtab search access point"
  );

  await setFeatureGate(registry, false);
  Assert.equal(
    searchComponentOf(registry).tagName,
    "content-search-handoff-ui",
    "The handoff search bar comes back once the gate is disabled again"
  );
});
