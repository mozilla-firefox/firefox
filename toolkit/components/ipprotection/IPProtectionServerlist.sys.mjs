/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This file contains functions that work on top of the RemoteSettings
 * Bucket for the IP Protection server list.
 */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "logConsole", () =>
  console.createInstance({
    logModule: "IPP_Serverlist",
    maxLogLevel: Services.prefs.getBoolPref("browser.ipProtection.log", false)
      ? "Debug"
      : "Warn",
  })
);

ChromeUtils.defineESModuleGetters(lazy, {
  IPPStartupCache:
    "moz-src:///toolkit/components/ipprotection/IPPStartupCache.sys.mjs",
  IPProtectionService:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
  IPProtectionStates:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
});

/**
 * Reserved country code used in the Remote Settings `vpn-serverlist`
 * collection to mark the anycast / recommended entry. Excluded from the
 * user-facing `countries` enumeration.
 */
export const RECOMMENDED_COUNTRY_CODE = "REC";

const EGRESS_LOCATION_PREF = "browser.ipProtection.egressLocation";

/**
 * Event dispatched by `IPProtectionServerlistBase` instances whenever the
 * underlying list has been replaced (RS sync, pref change, initial fetch).
 */
const LIST_CHANGED_EVENT = "IPProtectionServerlist:ListChanged";

/**
 *
 */
export class IProtocol {
  name = "";
  static construct(data) {
    switch (data.name) {
      case "masque":
        return new MasqueProtocol(data);
      case "connect":
        return new ConnectProtocol(data);
      default:
        throw new Error("Unknown protocol: " + data.name);
    }
  }
}

/**
 *
 */
export class MasqueProtocol extends IProtocol {
  name = "masque";
  host = "";
  port = 0;
  templateString = "";
  constructor(data) {
    super();
    this.host = data.host || "";
    this.port = data.port || 0;
    this.templateString = data.templateString || "";
  }
}

/**
 *
 */
export class ConnectProtocol extends IProtocol {
  name = "connect";
  host = "";
  port = 0;
  scheme = "https";
  constructor(data) {
    super();
    this.host = data.host || "";
    this.port = data.port || 0;
    this.scheme = data.scheme || "https";
  }
}

/**
 * Class representing a server.
 */
export class Server {
  /**
   * Port of the server
   *
   * @type {number}
   */
  port = 443;
  /**
   * Hostname of the server
   *
   * @type {string}
   */
  hostname = "";
  /**
   * If true the server is quarantined
   * and should not be used
   *
   * @type {boolean}
   */
  quarantined = false;

  /**
   * List of supported protocols
   *
   * @type {Array<MasqueProtocol|ConnectProtocol>}
   */
  protocols = [];

  constructor(data) {
    this.port = data.port || 443;
    this.hostname = data.hostname || "";
    this.quarantined = !!data.quarantined;
    this.protocols = (data.protocols || []).map(p => IProtocol.construct(p));

    // Default to connect if no protocols are specified
    if (this.protocols.length === 0) {
      this.protocols = [
        new ConnectProtocol({
          name: "connect",
          host: this.hostname,
          port: this.port,
        }),
      ];
    }
  }
}

/**
 * Class representing a city.
 */
class City {
  /**
   * Fallback name for the city if not available
   *
   * @type {string}
   */
  name = "";
  /**
   * A stable identifier for the city
   * (Usually a Wikidata ID)
   *
   * @type {string}
   */
  code = "";
  /**
   * List of servers in this city
   *
   * @type {Server[]}
   */
  servers = [];

  constructor(data) {
    this.name = data.name || "";
    this.code = data.code || "";
    this.servers = (data.servers || []).map(s => new Server(s));
  }
}

/**
 * Class representing a country.
 */
class Country {
  /**
   * Fallback name for the country if not available
   *
   * @type {string}
   */
  name;
  /**
   * A stable identifier for the country
   * Usually a ISO 3166-1 alpha-2 code
   *
   * @type {string}
   */
  code;

  /**
   * List of cities in this country
   *
   * @type {City[]}
   */
  cities;

  /**
   * If true this Location is locked behind a set of preconditions
   */
  locked = false;

  constructor(data) {
    this.name = data.name || "";
    this.code = data.code || "";
    this.cities = (data.cities || []).map(c => new City(c));
    this.locked = !!data.locked;
  }
}

/**
 * Base Class for the Serverlist
 */
export class IPProtectionServerlistBase extends EventTarget {
  /**
   * @type {Country[]}
   */
  __list = [];

  constructor() {
    super();
    this.handleEvent = this.#handleEvent.bind(this);
  }

  init() {
    lazy.IPProtectionService.addEventListener(
      "IPProtectionService:StateChanged",
      this.handleEvent
    );
  }
  uninit() {
    lazy.IPProtectionService.removeEventListener(
      "IPProtectionService:StateChanged",
      this.handleEvent
    );
  }
  #handleEvent(_event) {
    this.#refreshWhenReady();
  }

  async initOnStartupCompleted() {
    this.#refreshWhenReady();
  }

  // Not before startup has completed: the source may not be readable yet.
  #refreshWhenReady() {
    if (
      lazy.IPPStartupCache.isStartupCompleted &&
      lazy.IPProtectionService.state === lazy.IPProtectionStates.READY
    ) {
      this.maybeFetchList(true);
    }
  }

  /**
   * Tries to refresh the list from the underlining source.
   *
   * @param {*} _forceUpdate - if true, forces a refresh even if the list is already populated.
   */
  maybeFetchList(_forceUpdate = false) {
    throw new Error("Not implemented");
  }

  /**
   * Enumerates countries known to the serverlist, excluding the reserved
   * recommended (anycast) entry.
   *
   * @returns {Array<{code: string, available: boolean}>} - One entry per
   *   country. `code` is an ISO 3166-1 alpha-2 code. `available` is true iff
   *   the country has at least one city containing a non-quarantined server.
   */
  get countries() {
    return this.__list
      .filter(country => country.code !== RECOMMENDED_COUNTRY_CODE)
      .map(country => ({
        available: country.cities.some(city =>
          city.servers.some(server => !server.quarantined)
        ),
        code: country.code,
        locked: country.locked,
      }));
  }

  /**
   * Resolves a country code to a usable {country, city} pair.
   *
   * @param {string} [countryCode=RECOMMENDED_COUNTRY_CODE]
   *   ISO 3166-1 alpha-2 country code, or the reserved recommended code.
   *   Defaults to the recommended entry.
   * @returns {{country: Country, city: City}|null} - The first city with
   *   servers in the requested country, or null if the country is absent or
   *   has no usable city.
   */
  getLocation(countryCode = RECOMMENDED_COUNTRY_CODE) {
    const country = this.__list.find(c => c.code === countryCode);
    if (!country) {
      return null;
    }
    const city = country.cities.find(c => c.servers.length);
    if (!city) {
      return null;
    }
    return { country, city };
  }

  /**
   * Returns the recommended (anycast) location, falling back to the US entry
   * when the Remote Settings collection has not shipped a `REC` entry yet.
   *
   * @returns {{country: Country, city: City}|null}
   */
  getRecommendedLocation() {
    return this.getLocation(RECOMMENDED_COUNTRY_CODE) ?? this.getLocation("US");
  }

  /**
   * Resolves the egress location the user picked to the country to route
   * through. Locked countries are refused unless the user is premium.
   *
   * @param {(() => Promise<boolean>) | null} [isPremium]
   *   Whether the user may use locked countries. When omitted, they may not.
   * @returns {Promise<string|undefined>} - The country code, or undefined for
   *   the recommended location.
   */
  async resolveEgressCountry(isPremium) {
    const country =
      Services.prefs.getStringPref(EGRESS_LOCATION_PREF, "") || undefined;
    if (!country || !this.getLocation(country)?.country.locked) {
      return country;
    }
    try {
      if (await isPremium?.()) {
        return country;
      }
    } catch (e) {
      lazy.logConsole.error("Premium check failed:", e);
    }
    return undefined;
  }

  /**
   * Given a city, it selects an available server.
   *
   * @param {City?} city
   * @returns {Server|null}
   */
  selectServer(city) {
    if (!city) {
      return null;
    }

    const servers = city.servers.filter(server => !server.quarantined);
    if (servers.length === 1) {
      return servers[0];
    }

    if (servers.length > 1) {
      return servers[Math.floor(Math.random() * servers.length)];
    }

    return null;
  }

  get hasList() {
    return this.__list.length !== 0;
  }

  static dataToList(list) {
    if (!Array.isArray(list)) {
      return [];
    }
    return list.map(c => new Country(c));
  }
}

/**
 * Class representing the IP Protection Serverlist
 * fetched from Remote Settings.
 */
export class RemoteSettingsServerlist extends IPProtectionServerlistBase {
  #bucket = null;
  #runningPromise = null;

  async initOnStartupCompleted() {
    super.initOnStartupCompleted();
    this.bucket.on("sync", async () => {
      await this.maybeFetchList(true);
    });
  }

  maybeFetchList(forceUpdate = false) {
    if (this.__list.length !== 0 && !forceUpdate) {
      return Promise.resolve(false);
    }

    if (this.#runningPromise) {
      return this.#runningPromise;
    }

    const fetchList = async () => {
      this.__list = IPProtectionServerlistBase.dataToList(
        await this.bucket.get()
      );

      this.dispatchEvent(new Event(LIST_CHANGED_EVENT));
      return true;
    };

    this.#runningPromise = fetchList().finally(
      () => (this.#runningPromise = null)
    );

    return this.#runningPromise;
  }

  get bucket() {
    if (!this.#bucket) {
      this.#bucket = lazy.RemoteSettings("vpn-serverlist");
    }
    return this.#bucket;
  }
}
/**
 * Class representing the IP Protection Serverlist
 * from about:config preferences.
 */
export class PrefServerList extends IPProtectionServerlistBase {
  #observer = null;
  #previousList = null;

  constructor() {
    super();
    this.#observer = this.onPrefChange.bind(this);
    this.maybeFetchList();
  }

  onPrefChange() {
    this.maybeFetchList();
  }

  async initOnStartupCompleted() {
    super.initOnStartupCompleted();
    Services.prefs.addObserver(PrefServerList.PREF_NAME, this.#observer);
  }

  uninit() {
    super.uninit();
    Services.prefs.removeObserver(PrefServerList.PREF_NAME, this.#observer);
  }

  maybeFetchList(_forceUpdate = false) {
    const newList = Services.prefs.getStringPref(PrefServerList.PREF_NAME, "");

    if (newList === this.#previousList) {
      return Promise.resolve(false);
    }
    this.#previousList = newList;
    this.__list = IPProtectionServerlistBase.dataToList(
      PrefServerList.prefValue
    );
    this.dispatchEvent(new Event(LIST_CHANGED_EVENT));
    return Promise.resolve(true);
  }

  static get PREF_NAME() {
    return "browser.ipProtection.override.serverlist";
  }
  /**
   * Returns true if the preference has a valid value.
   */
  static get hasPrefValue() {
    return (
      Services.prefs.getPrefType(this.PREF_NAME) ===
        Services.prefs.PREF_STRING &&
      !!Services.prefs.getStringPref(this.PREF_NAME).length
    );
  }
  static get prefValue() {
    try {
      const value = Services.prefs.getStringPref(this.PREF_NAME);
      return JSON.parse(value);
    } catch (e) {
      lazy.logConsole.error(
        `IPProtection: Error parsing serverlist pref value: ${e}`
      );
      return null;
    }
  }
}
/**
 *
 * @returns {IPProtectionServerlistBase} - The appropriate serverlist implementation.
 */
export function IPProtectionServerlistFactory() {
  if (AppConstants.MOZ_ENTERPRISE) {
    return new PrefServerList();
  }
  return PrefServerList.hasPrefValue
    ? new PrefServerList()
    : new RemoteSettingsServerlist();
}

// Only check once which implementation to use.
const IPProtectionServerlist = IPProtectionServerlistFactory();

export { IPProtectionServerlist };
