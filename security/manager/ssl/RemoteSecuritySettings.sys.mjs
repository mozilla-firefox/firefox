/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { RemoteSettings } from "resource://services-settings/remote-settings.sys.mjs";

import { X509 } from "moz-src:///security/manager/ssl/X509.sys.mjs";

const SECURITY_STATE_BUCKET = "security-state";
const SECURITY_STATE_SIGNER = "onecrl.content-signature.mozilla.org";

const INTERMEDIATES_DL_PER_POLL_PREF =
  "security.remote_settings.intermediates.downloads_per_poll";
const INTERMEDIATES_DL_PARALLEL_REQUESTS =
  "security.remote_settings.intermediates.parallel_downloads";
const INTERMEDIATES_ENABLED_PREF =
  "security.remote_settings.intermediates.enabled";
const LOGLEVEL_PREF = "browser.policies.loglevel";

const CRLITE_FILTERS_ENABLED_PREF =
  "security.remote_settings.crlite_filters.enabled";

const CRLITE_FILTER_CHANNEL_PREF = "security.pki.crlite_channel";

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "securityStateDir", () =>
  PathUtils.join(PathUtils.profileDir, "security_state")
);

ChromeUtils.defineLazyGetter(lazy, "gTextDecoder", () => new TextDecoder());

ChromeUtils.defineLazyGetter(lazy, "log", () => {
  let { ConsoleAPI } = ChromeUtils.importESModule(
    "resource://gre/modules/Console.sys.mjs"
  );
  return new ConsoleAPI({
    prefix: "RemoteSecuritySettings",
    // tip: set maxLogLevel to "debug" and use log.debug() to create detailed
    // messages during development. See LOG_LEVELS in Console.sys.mjs for details.
    maxLogLevel: "error",
    maxLogLevelPref: LOGLEVEL_PREF,
  });
});
ChromeUtils.defineESModuleGetters(lazy, {
  RemoteSettingsClient:
    "resource://services-settings/RemoteSettingsClient.sys.mjs",
});

// Converts a JS string to an array of bytes consisting of the char code at each
// index in the string.
function stringToBytes(s) {
  let b = [];
  for (let i = 0; i < s.length; i++) {
    b.push(s.charCodeAt(i));
  }
  return b;
}

// Converts an array of bytes to a JS string using fromCharCode on each byte.
function bytesToString(bytes) {
  if (bytes.length > 65535) {
    throw new Error("input too long for bytesToString");
  }
  return String.fromCharCode.apply(null, bytes);
}

class CertInfo {
  constructor(cert, subject) {
    this.cert = cert;
    this.subject = subject;
    this.trust = Ci.nsICertStorage.TRUST_INHERIT;
  }
}
CertInfo.prototype.QueryInterface = ChromeUtils.generateQI(["nsICertInfo"]);

class RevocationState {
  constructor(state) {
    this.state = state;
  }
}

class IssuerAndSerialRevocationState extends RevocationState {
  constructor(issuer, serial, state) {
    super(state);
    this.issuer = issuer;
    this.serial = serial;
  }
}
IssuerAndSerialRevocationState.prototype.QueryInterface =
  ChromeUtils.generateQI(["nsIIssuerAndSerialRevocationState"]);

class SubjectAndPubKeyRevocationState extends RevocationState {
  constructor(subject, pubKey, state) {
    super(state);
    this.subject = subject;
    this.pubKey = pubKey;
  }
}
SubjectAndPubKeyRevocationState.prototype.QueryInterface =
  ChromeUtils.generateQI(["nsISubjectAndPubKeyRevocationState"]);

function setRevocations(certStorage, revocations) {
  return new Promise(resolve =>
    certStorage.setRevocations(revocations, resolve)
  );
}

/**
 * Helper function that returns a promise that will resolve with whether or not
 * the nsICertStorage implementation has prior data of the given type.
 *
 * @param {Integer} dataType a Ci.nsICertStorage.DATA_TYPE_* constant
 *                           indicating the type of data

 * @returns {Promise} a promise that will resolve with true if the data type is
 *                   present
 */
function hasPriorData(dataType) {
  let certStorage = Cc["@mozilla.org/security/certstorage;1"].getService(
    Ci.nsICertStorage
  );
  return new Promise(resolve => {
    certStorage.hasPriorData(dataType, (rv, out) => {
      if (rv == Cr.NS_OK) {
        resolve(out);
      } else {
        // If calling hasPriorData failed, assume we need to reload everything
        // (even though it's unlikely doing so will succeed).
        resolve(false);
      }
    });
  });
}

/**
 * Helper function that returns a promise that will resolve with the set of
 * SHA256 hashes of CRLite filters that are currently loaded in cert_storage.
 *
 * @param {object} certStorage a Ci.nsICertStorage instance

 * @returns {Promise} a promise that will resolve with the set of hashes.
 */
function getLoadedCRLiteFilterHashes(certStorage) {
  return new Promise(resolve => {
    certStorage.getCRLiteFilterHashes((rv, result) => {
      if (rv != Cr.NS_OK || !result) {
        resolve(new Set());
        return;
      }
      let str = result.toString();
      resolve(new Set(str ? str.split(",") : []));
    });
  });
}

/**
 * Revoke the appropriate certificates based on the records from the blocklist.
 *
 * @param {object} options
 * @param {object} options.data Current records in the local db.
 * @param {Array} options.data.current
 * @param {Array} options.data.created
 * @param {Array} options.data.updated
 * @param {Array} options.data.deleted
 */
const updateCertBlocklist = async function ({
  data: { current, created, updated, deleted },
}) {
  let items = [];

  // See if we have prior revocation data (this can happen when we can't open
  // the database and we have to re-create it (see bug 1546361)).
  let hasPriorRevocationData = await hasPriorData(
    Ci.nsICertStorage.DATA_TYPE_REVOCATION
  );

  // If we don't have prior data, make it so we re-load everything.
  if (!hasPriorRevocationData) {
    deleted = [];
    updated = [];
    created = current;
  }

  let toDelete = deleted.concat(updated.map(u => u.old));
  for (let item of toDelete) {
    if (item.issuerName && item.serialNumber) {
      items.push(
        new IssuerAndSerialRevocationState(
          item.issuerName,
          item.serialNumber,
          Ci.nsICertStorage.STATE_UNSET
        )
      );
    } else if (item.subject && item.pubKeyHash) {
      items.push(
        new SubjectAndPubKeyRevocationState(
          item.subject,
          item.pubKeyHash,
          Ci.nsICertStorage.STATE_UNSET
        )
      );
    }
  }

  const toAdd = created.concat(updated.map(u => u.new));

  for (let item of toAdd) {
    if (item.issuerName && item.serialNumber) {
      items.push(
        new IssuerAndSerialRevocationState(
          item.issuerName,
          item.serialNumber,
          Ci.nsICertStorage.STATE_ENFORCE
        )
      );
    } else if (item.subject && item.pubKeyHash) {
      items.push(
        new SubjectAndPubKeyRevocationState(
          item.subject,
          item.pubKeyHash,
          Ci.nsICertStorage.STATE_ENFORCE
        )
      );
    }
  }

  try {
    const certList = Cc["@mozilla.org/security/certstorage;1"].getService(
      Ci.nsICertStorage
    );
    await setRevocations(certList, items);
  } catch (e) {
    lazy.log.error(e);
  }
};

export var RemoteSecuritySettings = {
  _initialized: false,
  OneCRLBlocklistClient: null,
  IntermediatePreloadsClient: null,
  CRLiteFiltersClient: null,

  /**
   * Initialize the clients (cheap instantiation) and setup their sync event.
   * This static method is called from BrowserGlue.sys.mjs soon after startup.
   *
   * @returns {object} instantiated clients for security remote settings.
   */
  init() {
    // Avoid repeated initialization (work-around for bug 1730026).
    if (this._initialized) {
      return this;
    }
    this._initialized = true;

    this.OneCRLBlocklistClient = RemoteSettings("onecrl", {
      bucketName: SECURITY_STATE_BUCKET,
      signerName: SECURITY_STATE_SIGNER,
    });
    this.OneCRLBlocklistClient.on("sync", updateCertBlocklist);
    // Ensure the bundled list of revocations is loaded.
    this.OneCRLBlocklistClient.get()
      .then(records => {
        return updateCertBlocklist({
          data: {
            current: records,
            created: records,
            updated: [],
            deleted: [],
          },
        });
      })
      .catch(e => {
        lazy.log.error(e);
      })
      .finally(() => {
        // Only used by tests, to know when the bundled list is in cert_storage.
        Services.obs.notifyObservers(
          null,
          "remote-security-settings:onecrl-bundle-loaded"
        );
      });

    this.IntermediatePreloadsClient = new IntermediatePreloads();

    this.CRLiteFiltersClient = new CRLiteFilters();

    return this;
  },
};

class IntermediatePreloads {
  constructor() {
    this.client = RemoteSettings("intermediates", {
      bucketName: SECURITY_STATE_BUCKET,
      signerName: SECURITY_STATE_SIGNER,
      localFields: ["cert_import_complete"],
    });

    this.client.on("sync", this.onSync.bind(this));
    Services.obs.addObserver(
      this.onObservePollEnd.bind(this),
      "remote-settings:changes-poll-end"
    );

    lazy.log.debug("Intermediate Preloading: constructor");
  }

  async updatePreloadedIntermediates() {
    if (!Services.prefs.getBoolPref(INTERMEDIATES_ENABLED_PREF, true)) {
      lazy.log.debug("Intermediate Preloading is disabled");
      Services.obs.notifyObservers(
        null,
        "remote-security-settings:intermediates-updated",
        "disabled"
      );
      return;
    }

    // Download attachments that are awaiting download, up to a max.
    const maxDownloadsPerRun = Services.prefs.getIntPref(
      INTERMEDIATES_DL_PER_POLL_PREF,
      100
    );
    const parallelDownloads = Services.prefs.getIntPref(
      INTERMEDIATES_DL_PARALLEL_REQUESTS,
      8
    );

    // Bug 1519256: Move this to a separate method that's on a separate timer
    // with a higher frequency (so we can attempt to download outstanding
    // certs more than once daily)

    // See if we have prior cert data (this can happen when we can't open the database and we
    // have to re-create it (see bug 1546361)).
    let hasPriorCertData = await hasPriorData(
      Ci.nsICertStorage.DATA_TYPE_CERTIFICATE
    );
    // If we don't have prior data, make it so we re-load everything.
    if (!hasPriorCertData) {
      let current = [];
      try {
        current = await this.client.db.list();
      } catch (err) {
        if (!(err instanceof lazy.RemoteSettingsClient.EmptyDatabaseError)) {
          lazy.log.warn(
            `Unable to list intermediate preloading collection: ${err}`
          );
          return;
        }
      }
      const toReset = current.filter(record => record.cert_import_complete);
      try {
        await this.client.db.importChanges(
          undefined, // do not touch metadata.
          undefined, // do not touch collection timestamp.
          toReset.map(r => ({ ...r, cert_import_complete: false }))
        );
      } catch (err) {
        lazy.log.warn(
          `Unable to update intermediate preloading collection: ${err}`
        );
        return;
      }
    }

    let current = [];
    try {
      current = await this.client.db.list();
    } catch (err) {
      if (!(err instanceof lazy.RemoteSettingsClient.EmptyDatabaseError)) {
        lazy.log.warn(
          `Unable to list intermediate preloading collection: ${err}`
        );
        return;
      }
    }

    // fetch attachment bundle if we are initializing cert data or remote-settings records
    if (!hasPriorCertData || !current.length) {
      try {
        // download() is called further down to force a re-sync on hash mismatches for old data or if the bundle fails to download
        await this.client.attachments.cacheAll();
      } catch (err) {
        lazy.log.warn(
          `Error fetching/caching attachment bundle in intermediate preloading: ${err}`
        );
      }
    }

    const waiting = current.filter(record => !record.cert_import_complete);

    lazy.log.debug(
      `There are ${waiting.length} intermediates awaiting download.`
    );
    if (!waiting.length) {
      // Nothing to do.
      Services.obs.notifyObservers(
        null,
        "remote-security-settings:intermediates-updated",
        "success"
      );
      return;
    }

    let toDownload = waiting.slice(0, maxDownloadsPerRun);
    let recordsCertsAndSubjects = [];
    for (let i = 0; i < toDownload.length; i += parallelDownloads) {
      const chunk = toDownload.slice(i, i + parallelDownloads);
      const downloaded = await Promise.all(
        chunk.map(record => this.maybeDownloadAttachment(record))
      );
      recordsCertsAndSubjects = recordsCertsAndSubjects.concat(downloaded);
    }

    let certInfos = [];
    let recordsToUpdate = [];
    for (let { record, cert, subject } of recordsCertsAndSubjects) {
      if (cert && subject) {
        certInfos.push(new CertInfo(cert, subject));
        recordsToUpdate.push(record);
      }
    }
    const certStorage = Cc["@mozilla.org/security/certstorage;1"].getService(
      Ci.nsICertStorage
    );
    let result = await new Promise(resolve => {
      certStorage.addCerts(certInfos, resolve);
    }).catch(err => err);
    if (result != Cr.NS_OK) {
      lazy.log.error(`certStorage.addCerts failed: ${result}`);
      return;
    }
    try {
      await this.client.db.importChanges(
        undefined, // do not touch metadata.
        undefined, // do not touch collection timestamp.
        recordsToUpdate.map(r => ({ ...r, cert_import_complete: true }))
      );
    } catch (err) {
      lazy.log.warn(
        `Unable to update intermediate preloading collection: ${err}`
      );
      return;
    }

    // attachment cache is no longer needed
    await this.client.attachments.deleteAll();

    Services.obs.notifyObservers(
      null,
      "remote-security-settings:intermediates-updated",
      "success"
    );
  }

  async onObservePollEnd(subject, topic) {
    lazy.log.debug(`onObservePollEnd ${subject} ${topic}`);

    try {
      await this.updatePreloadedIntermediates();
    } catch (err) {
      lazy.log.warn(`Unable to update intermediate preloads: ${err}`);
    }
  }

  // This method returns a promise to RemoteSettingsClient.maybeSync method.
  async onSync({ data: { deleted } }) {
    if (!Services.prefs.getBoolPref(INTERMEDIATES_ENABLED_PREF, true)) {
      lazy.log.debug("Intermediate Preloading is disabled");
      return;
    }

    lazy.log.debug(`Removing ${deleted.length} Intermediate certificates`);
    await this.removeCerts(deleted);
  }

  /**
   * Attempts to download the attachment, assuming it's not been processed
   * already. Does not retry, and always resolves (e.g., does not reject upon
   * failure.) Errors are reported via console.error.
   *
   * @param  {AttachmentRecord} record defines which data to obtain
   * @returns {Promise}          a Promise that will resolve to an object with the properties
   *                            record, cert, and subject. record is the original record.
   *                            cert is the base64-encoded bytes of the downloaded certificate (if
   *                            downloading was successful), and null otherwise.
   *                            subject is the base64-encoded bytes of the subject distinguished
   *                            name of the same.
   */
  async maybeDownloadAttachment(record) {
    let result = { record, cert: null, subject: null };

    let dataAsString = null;
    try {
      let { buffer } = await this.client.attachments.download(record, {
        retries: 0,
        checkHash: true,
        cacheResult: false,
      });
      dataAsString = lazy.gTextDecoder.decode(new Uint8Array(buffer));
    } catch (err) {
      if (err.name == "BadContentError") {
        lazy.log.debug(`Bad attachment content.`);
      } else {
        lazy.log.error(`Failed to download attachment: ${err}`);
      }
      return result;
    }

    let certBase64;
    let subjectBase64;
    try {
      // split off the header and footer
      certBase64 = dataAsString.split("-----")[2].replace(/\s/g, "");
      // get an array of bytes so we can use X509.sys.mjs
      let certBytes = stringToBytes(atob(certBase64));
      let cert = new X509.Certificate();
      cert.parse(certBytes);
      // get the DER-encoded subject and get a base64-encoded string from it
      // TODO(bug 1542028): add getters for _der and _bytes
      subjectBase64 = btoa(
        bytesToString(cert.tbsCertificate.subject._der._bytes)
      );
    } catch (err) {
      lazy.log.error(`Failed to decode cert: ${err}`);
      return result;
    }
    result.cert = certBase64;
    result.subject = subjectBase64;
    return result;
  }

  async maybeSync(expectedTimestamp, options) {
    return this.client.maybeSync(expectedTimestamp, options);
  }

  async removeCerts(recordsToRemove) {
    let certStorage = Cc["@mozilla.org/security/certstorage;1"].getService(
      Ci.nsICertStorage
    );
    let hashes = recordsToRemove.map(record => record.derHash);
    let result = await new Promise(resolve => {
      certStorage.removeCertsByHashes(hashes, resolve);
    }).catch(err => err);
    if (result != Cr.NS_OK) {
      lazy.log.error(`Failed to remove some intermediate certificates`);
    }
  }
}

/**
 * Check whether a CRLite filter is on disk in the security_state directory with the correct hash.
 *
 * @param {object} filter A CRLite filter record from Remote Settings.
 * @returns {boolean} True if the file is on disk with the correct size and SHA-256 hash.
 */
async function isFilterOnDisk(filter) {
  let filename = filter.incremental
    ? filter.attachment.filename
    : "crlite.filter";
  let filePath = PathUtils.join(lazy.securityStateDir, filename);
  let bytes;
  try {
    bytes = await IOUtils.read(filePath);
  } catch (_) {
    return false;
  }
  let { size, hash } = filter.attachment;
  if (bytes.length !== size) {
    return false;
  }
  const hashBuffer = await crypto.subtle.digest("SHA-256", bytes);
  return new Uint8Array(hashBuffer).toHex() === hash;
}

class CRLiteFilters {
  constructor() {
    this.client = RemoteSettings("cert-revocations", {
      bucketName: SECURITY_STATE_BUCKET,
      signerName: SECURITY_STATE_SIGNER,
    });

    Services.obs.addObserver(
      this.onObservePollEnd.bind(this),
      "remote-settings:changes-poll-end"
    );
    Services.prefs.addObserver(CRLITE_FILTER_CHANNEL_PREF, this);
  }

  observe(subject, topic, prefName) {
    if (topic == "nsPref:changed" && prefName == CRLITE_FILTER_CHANNEL_PREF) {
      this.onObservePollEnd();
    }
  }

  async readOrDownloadFilter(filter, tryDisk) {
    if (tryDisk) {
      let filename = filter.incremental
        ? filter.attachment.filename
        : "crlite.filter";
      try {
        return await IOUtils.read(
          PathUtils.join(lazy.securityStateDir, filename)
        );
      } catch (e) {
        lazy.log.error("failed to read CRLite filter from disk", e);
      }
    }
    try {
      return new Uint8Array(
        await this.client.attachments.downloadAsBytes(filter)
      );
    } catch (e) {
      lazy.log.error("failed to download CRLite filter", e);
      return null;
    }
  }

  async getFilteredRecords() {
    let records = [];
    try {
      records = await this.client.db.list();
    } catch (err) {
      if (!(err instanceof lazy.RemoteSettingsClient.EmptyDatabaseError)) {
        throw err;
      }
    }
    records = await this.client._filterEntries(records);
    return records;
  }

  async onObservePollEnd() {
    if (!Services.prefs.getBoolPref(CRLITE_FILTERS_ENABLED_PREF, true)) {
      lazy.log.debug("CRLite filter downloading is disabled");
      Services.obs.notifyObservers(
        null,
        "remote-security-settings:crlite-filters-updated",
        "disabled"
      );
      return;
    }

    let records = await this.getFilteredRecords();
    let fullFilters = records.filter(filter => !filter.incremental);
    if (fullFilters.length != 1) {
      lazy.log.debug(
        `${fullFilters.length} full CRLite filters available to download, expected 1`
      );
      Services.obs.notifyObservers(
        null,
        "remote-security-settings:crlite-filters-updated",
        "unavailable"
      );
      return;
    }
    let fullFilter = fullFilters[0];
    let incrementalFilters = records.filter(filter => filter.incremental);
    const certList = Cc["@mozilla.org/security/certstorage;1"].getService(
      Ci.nsICertStorage
    );
    let loadedFilterHashes = await getLoadedCRLiteFilterHashes(certList);

    lazy.log.debug("filtersInChannel:", records);
    let filtersToInstall = [];
    let filtersDownloaded = [];
    let fullFilterInstalling = false;

    let expectedDeltaHashes = new Set(
      incrementalFilters.map(f => f.attachment.hash)
    );
    // Check for stale deltas: hashes loaded in cert_storage that are not in
    // the expected set (e.g. deltas from a previous channel). setFullCRLiteFilter
    // clears all loaded deltas, so we can re-load the full filter to remove them.
    let hasStaleLoadedDeltas = [...loadedFilterHashes]
      .filter(h => h !== fullFilter.attachment.hash)
      .some(h => !expectedDeltaHashes.has(h));

    let fullFilterOnDisk = await isFilterOnDisk(fullFilter);
    if (
      !fullFilterOnDisk ||
      !loadedFilterHashes.has(fullFilter.attachment.hash) ||
      hasStaleLoadedDeltas
    ) {
      // setFullCRLiteFilter clears all loaded deltas from cert_storage and
      // removes delta files from the security_state directory. In the event of,
      // say, a corrupted full filter on disk, we would like to avoid
      // re-downloading any delta files that we have good copies of on disk.
      // The fullFilterInstalling flag causes us to read those deltas from disk
      // prior to calling setFullCRLiteFilter so that we can re-load them
      // without fetching them over the network.
      fullFilterInstalling = true;
      let bytes = await this.readOrDownloadFilter(fullFilter, fullFilterOnDisk);
      if (!bytes) {
        Services.obs.notifyObservers(
          null,
          "remote-security-settings:crlite-filters-updated",
          "unavailable"
        );
        return;
      }
      lazy.log.debug(`${fullFilter.details.name}: ${bytes.length} bytes`);
      fullFilter.bytes = bytes;
      if (!fullFilterOnDisk) {
        filtersDownloaded.push(fullFilter.details.name);
      }
      filtersToInstall.push(fullFilter);
    }

    for (let filter of incrementalFilters) {
      let deltaOnDisk = await isFilterOnDisk(filter);
      if (
        !fullFilterInstalling &&
        deltaOnDisk &&
        loadedFilterHashes.has(filter.attachment.hash)
      ) {
        continue;
      }
      let bytes = await this.readOrDownloadFilter(filter, deltaOnDisk);
      if (!bytes) {
        lazy.log.error(
          `failed to obtain bytes for CRLite delta ${filter.attachment.filename}`
        );
        continue;
      }
      lazy.log.debug(`${filter.details.name}: ${bytes.length} bytes`);
      filter.bytes = bytes;
      if (!deltaOnDisk) {
        filtersDownloaded.push(filter.details.name);
      }
      filtersToInstall.push(filter);
    }

    let fullFilterToInstall = filtersToInstall.find(f => !f.incremental);
    if (fullFilterToInstall) {
      await new Promise(resolve => {
        certList.setFullCRLiteFilter(fullFilterToInstall.bytes, rv => {
          lazy.log.debug(`setFullCRLiteFilter: ${rv}`);
          resolve();
        });
      });
    }
    for (let filter of filtersToInstall.filter(f => f.incremental)) {
      lazy.log.debug(`adding delta update of size ${filter.bytes.length}`);
      await new Promise(resolve => {
        certList.addCRLiteDelta(
          filter.bytes,
          filter.attachment.filename,
          rv => {
            lazy.log.debug(`addCRLiteDelta: ${rv}`);
            resolve();
          }
        );
      });
    }

    for (let filter of filtersToInstall) {
      delete filter.bytes;
    }

    Services.obs.notifyObservers(
      null,
      "remote-security-settings:crlite-filters-updated",
      `finished;${filtersToInstall.map(f => f.details.name).join(",")};${filtersDownloaded.join(",")}`
    );
  }
}
