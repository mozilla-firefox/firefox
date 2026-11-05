/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  JsonSchema: "resource://gre/modules/JsonSchema.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
});
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

ChromeUtils.defineLazyGetter(lazy, "logConsole", () => {
  return console.createInstance({
    logModule: "FingerprintingWebCompatService",
    maxLogLevelPref:
      "privacy.fingerprintingProtection.WebCompatService.logLevel",
  });
});

// A domain is either "*" or a hostname whose last label is either alphabetic
// (2-63 characters, covering long gTLDs such as .photography) or an IDN TLD in
// punycode form (xn--...).
const DOMAIN_PATTERN =
  "^(\\*|(?:(?!-)[A-Za-z0-9-]{1,63}(?<!-)\\.)+(?:[A-Za-z]{2,63}|xn--[A-Za-z0-9-]{1,59}))$";

// Note: this must be an object. JsonSchema.Validator silently accepts any
// instance when it is handed a JSON string instead of a schema object.
const SCHEMA = {
  type: "object",
  title: "Fingerprinting Overrides",
  $schema: "http://json-schema.org/draft-07/schema#",
  required: ["firstPartyDomain", "overrides"],
  properties: {
    overrides: {
      type: "string",
      pattern: "^[+-][A-Za-z]+(?:,[+-][A-Za-z]+)*$",
      description:
        "The fingerprinting overrides. See https://searchfox.org/firefox-main/source/toolkit/components/resistfingerprinting/RFPTargets.inc for details.",
    },
    firstPartyDomain: {
      type: "string",
      pattern: DOMAIN_PATTERN,
      description:
        "The first-party domain associated with the override. Use '*' to match all domains. Only legit domains allowed.",
    },
    thirdPartyDomain: {
      type: "string",
      // An empty string is treated the same as an absent thirdPartyDomain.
      pattern: `^$|${DOMAIN_PATTERN}`,
      description:
        "The third-party domain associated with the override. Use '*' to match all domains. Only legit domains allowed. Leave this field empty if the override is only for the first-party context.",
    },
  },
};

const COLLECTION_NAME = "fingerprinting-protection-overrides";
const PREF_GRANULAR_OVERRIDES =
  "privacy.fingerprintingProtection.granularOverrides";
const PREF_GRANULAR_OVERRIDES_BASELINE =
  "privacy.baselineFingerprintingProtection.granularOverrides";
const PREF_REMOTE_OVERRIDES_ENABLED =
  "privacy.fingerprintingProtection.remoteOverrides.enabled";

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "granularOverridesPref",
  PREF_GRANULAR_OVERRIDES
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "baselineGranularOverridesPref",
  PREF_GRANULAR_OVERRIDES_BASELINE
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "remoteOverridesEnabled",
  PREF_REMOTE_OVERRIDES_ENABLED
);

/**
 * The object represents a fingerprinting override.
 */
export class FingerprintingOverride {
  classID = Components.ID("{07f45442-1806-44be-9230-12eb79de9bac}");
  QueryInterface = ChromeUtils.generateQI(["nsIFingerprintingOverride"]);

  constructor(firstPartyDomain, thirdPartyDomain, overrides, isBaseline) {
    this.firstPartyDomain = firstPartyDomain;
    this.thirdPartyDomain = thirdPartyDomain;
    this.overrides = overrides;
    this.isBaseline = isBaseline;
  }
}

/**
 * The singleton service that is responsible for the WebCompat of the
 * fingerprinting protection. It gets fingerprinting overrides from remote
 * settings and the local test pref.
 */
export class FingerprintingWebCompatService {
  classId = Components.ID("{e7b1da06-2594-4670-aea4-131070baca4c}");
  QueryInterface = ChromeUtils.generateQI([
    "nsIFingerprintingWebCompatService",
  ]);
  #initialized = false;
  #remoteOverrides;
  #granularOverrides;
  #rs;
  #validator;

  #isParentProcess;

  constructor() {
    this.#isParentProcess =
      Services.appinfo.processType === Services.appinfo.PROCESS_TYPE_DEFAULT;

    this.#remoteOverrides = new Set();
    this.#granularOverrides = new Set();

    this.#rs = lazy.RemoteSettings(COLLECTION_NAME);
  }

  async init() {
    lazy.logConsole.debug("init");
    // We can only access remote settings from the parent process. So, never
    // init it in the content process.
    if (!this.#isParentProcess) {
      throw new Error(
        "Shouldn't init FingerprintingWebCompatService in content processes."
      );
    }

    // Return if we have initiated.
    if (this.#initialized) {
      return;
    }
    this.#initialized = true;

    // Register listener to import overrides when the overrides pref changes.
    Services.prefs.addObserver(PREF_GRANULAR_OVERRIDES, this);
    Services.prefs.addObserver(PREF_GRANULAR_OVERRIDES_BASELINE, this);
    Services.prefs.addObserver(PREF_REMOTE_OVERRIDES_ENABLED, this);

    // Register the sync event for the remote settings updates.
    this.#rs.on("sync", async _ => {
      await this.#importRemoteSettingsOverrides();
      this.#populateOverrides();
    });

    // Get the remote overrides from the remote settings.
    await this.#importRemoteSettingsOverrides();

    // Get the granular overrides from the pref.
    this.#importPrefOverrides();

    // Populate the overrides to the nsRFPService.
    this.#populateOverrides();

    lazy.logConsole.debug("Init completes");
  }

  // Lazily create the schema validator when needed
  #getSchemaValidator() {
    return (this.#validator ??= new lazy.JsonSchema.Validator(SCHEMA));
  }

  // Import fingerprinting overrides from the local granular pref.
  #importPrefOverrides() {
    lazy.logConsole.debug("importLocalGranularOverrides");

    // Clear overrides before we update.
    this.#granularOverrides.clear();

    for (const [pref, isBaseline] of [
      [lazy.baselineGranularOverridesPref, true],
      [lazy.granularOverridesPref, false],
    ]) {
      let overrides;
      try {
        overrides = JSON.parse(pref || "[]");
      } catch (error) {
        // Skip only this pref; a malformed baseline pref must not prevent the
        // FPP pref (or vice versa) from being imported.
        lazy.logConsole.error(
          `Failed to parse granular override JSON string: Not a valid JSON.`,
          error
        );
        continue;
      }

      // Ensure we have an array we can iterate over and not an object.
      if (!Array.isArray(overrides)) {
        lazy.logConsole.error(
          "Failed to parse granular overrides JSON String: Not an array."
        );
        continue;
      }

      // Skip creating the validator if there's nothing to validate.
      if (overrides.length === 0) {
        continue;
      }

      const validator = this.#getSchemaValidator();
      for (let override of overrides) {
        // Validate the override.
        let { valid, errors } = validator.validate(override);

        if (!valid) {
          lazy.logConsole.debug("Override validation error", override, errors);
          continue;
        }

        this.#granularOverrides.add(
          this.#createFingerprintingOverrideFrom(override, isBaseline)
        );
      }
    }
  }

  // Import fingerprinting overrides from the remote settings.
  async #importRemoteSettingsOverrides() {
    lazy.logConsole.debug("importRemoteSettingsOverrides");

    let entries;
    try {
      entries = await this.#rs.get();
    } catch (e) {}

    this.#onRemoteUpdate(entries || []);
  }

  #onRemoteUpdate(entries) {
    lazy.logConsole.debug("onUpdateEntries", { entries });

    if (!lazy.remoteOverridesEnabled) {
      lazy.logConsole.debug("Abort remote overrides");
      return;
    }

    // Clear all overrides before we update the overrides.
    this.#remoteOverrides.clear();

    for (let entry of entries) {
      this.#remoteOverrides.add(this.#createFingerprintingOverrideFrom(entry));
    }
  }

  #createFingerprintingOverrideFrom(entry, isBaselineOverride = null) {
    // If the isBaselineOverride is not provided, we will use the value from the
    // entry. If the entry doesn't have the isBaseline field, we will default to
    // false.
    // We default to false because we didn't have the baseline mode in the past,
    // and the existing entries don't have the isBaseline field.
    const isBaseline = isBaselineOverride ?? entry.isBaseline ?? false;
    return new FingerprintingOverride(
      entry.firstPartyDomain,
      entry.thirdPartyDomain,
      entry.overrides,
      isBaseline
    );
  }

  #populateOverrides() {
    lazy.logConsole.debug("populateOverrides");

    // Create the array that contains all overrides. We explicitly concat the
    // overrides from testing pref after the ones from remote settings to ensure
    // that the testing pref will take precedence.
    let overrides = Array.from(this.#remoteOverrides).concat(
      Array.from(this.#granularOverrides)
    );

    // Set the remote override to the RFP service.
    Services.rfp.setFingerprintingOverrides(overrides);
  }

  observe(subject, topic, prefName) {
    if (
      prefName != PREF_GRANULAR_OVERRIDES &&
      prefName != PREF_GRANULAR_OVERRIDES_BASELINE &&
      prefName != PREF_REMOTE_OVERRIDES_ENABLED
    ) {
      return;
    }

    if (prefName === PREF_REMOTE_OVERRIDES_ENABLED) {
      if (!lazy.remoteOverridesEnabled) {
        this.#remoteOverrides.clear();
        this.#populateOverrides();
      } else {
        this.#importRemoteSettingsOverrides().then(() =>
          this.#populateOverrides()
        );
      }
      return;
    }

    this.#importPrefOverrides();
    this.#populateOverrides();
  }

  refreshOverrides() {
    lazy.logConsole.debug("refreshOverrides");

    // Nothing has been imported yet, so there is nothing to refresh.
    if (!this.#initialized) {
      return;
    }

    this.#populateOverrides();
  }

  shutdown() {
    lazy.logConsole.debug("shutdown");

    Services.prefs.removeObserver(PREF_GRANULAR_OVERRIDES, this);
    Services.prefs.removeObserver(PREF_GRANULAR_OVERRIDES_BASELINE, this);
    Services.prefs.removeObserver(PREF_REMOTE_OVERRIDES_ENABLED, this);
  }
}
