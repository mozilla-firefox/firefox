/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  TestUtils: "resource://testing-common/TestUtils.sys.mjs",
});

const kConfigCollectionKey = "doh-config";
const kProviderCollectionKey = "doh-providers";

const kConfigUpdateTopic = "doh-config-updated";
const kControllerReloadedTopic = "doh:controller-reloaded";

/*
 * Some helpers for loading and modifying DoH config in
 * Remote Settings. Call resetRemoteSettingsConfig to set up
 * basic default config that omits external URLs. Use
 * waitForConfigFlush to wait for DoH actors to pick up changes.
 *
 * Some tests need to load/reset config while DoH actors are
 * uninitialized. Pass waitForConfigFlushes = false in these cases.
 */
export const DoHTestUtils = {
  providers: [
    {
      uri: "https://example.com/1",
      UIName: "Example 1",
      autoDefault: false,
      canonicalName: "",
      id: "example-1",
    },
    {
      uri: "https://example.com/2",
      UIName: "Example 2",
      autoDefault: false,
      canonicalName: "",
      id: "example-2",
    },
  ],

  async loadRemoteSettingsProviders(providers, waitForConfigFlushes = true) {
    let configFlushedPromise = this.waitForConfigFlush(waitForConfigFlushes);

    let providerRS = lazy.RemoteSettings(kProviderCollectionKey);
    let db = await providerRS.db;
    await db.importChanges({}, Date.now(), providers, { clear: true });

    // Trigger a sync.
    await this.triggerSync(providerRS);

    await configFlushedPromise;
  },

  async loadRemoteSettingsConfig(config, waitForConfigFlushes = true) {
    let configFlushedPromise = this.waitForConfigFlush(waitForConfigFlushes);

    let configRS = lazy.RemoteSettings(kConfigCollectionKey);
    let db = await configRS.db;
    await db.importChanges({}, Date.now(), [config]);

    // Trigger a sync.
    await this.triggerSync(configRS);

    await configFlushedPromise;
  },

  // Loads default config for testing without clearing existing entries.
  async loadDefaultRemoteSettingsConfig(waitForConfigFlushes = true) {
    await this.loadRemoteSettingsProviders(
      this.providers,
      waitForConfigFlushes
    );

    await this.loadRemoteSettingsConfig(
      {
        providers: "example-1, example-2",
        rolloutEnabled: false,
        steeringEnabled: false,
        steeringProviders: "",
        autoDefaultEnabled: false,
        autoDefaultProviders: "",
        id: "global",
      },
      waitForConfigFlushes
    );
  },

  // Clears existing config AND loads defaults.
  async resetRemoteSettingsConfig(waitForConfigFlushes = true) {
    let providerRS = lazy.RemoteSettings(kProviderCollectionKey);
    let configRS = lazy.RemoteSettings(kConfigCollectionKey);
    for (let rs of [providerRS, configRS]) {
      let configFlushedPromise = this.waitForConfigFlush(waitForConfigFlushes);
      await rs.db.importChanges({}, Date.now(), [], { clear: true });
      // Trigger a sync to clear.
      await this.triggerSync(rs);
      await configFlushedPromise;
    }

    await this.loadDefaultRemoteSettingsConfig(waitForConfigFlushes);
  },

  triggerSync(rs) {
    return rs.emit("sync", {
      data: {
        current: [],
      },
    });
  },

  waitForConfigUpdate() {
    return lazy.TestUtils.topicObserved(kConfigUpdateTopic);
  },

  waitForControllerReload() {
    return lazy.TestUtils.topicObserved(kControllerReloadedTopic);
  },

  waitForConfigFlush(shouldWait = true) {
    if (!shouldWait) {
      return Promise.resolve();
    }

    return Promise.all([
      this.waitForConfigUpdate(),
      this.waitForControllerReload(),
    ]);
  },
};
