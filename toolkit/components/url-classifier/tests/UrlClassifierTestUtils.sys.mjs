const ANNOTATION_TABLE_NAME = "mochitest1-track-simple";
const ANNOTATION_TABLE_PREF = "urlclassifier.trackingAnnotationTable";
const ANNOTATION_ENTITYLIST_TABLE_NAME = "mochitest1-trackwhite-simple";
const ANNOTATION_ENTITYLIST_TABLE_PREF =
  "urlclassifier.trackingAnnotationWhitelistTable";

const TRACKING_TABLE_NAME = "mochitest2-track-simple";
const TRACKING_TABLE_PREF = "urlclassifier.trackingTable";
const ENTITYLIST_TABLE_NAME = "mochitest2-trackwhite-simple";
const ENTITYLIST_TABLE_PREF = "urlclassifier.trackingWhitelistTable";

const SOCIAL_ANNOTATION_TABLE_NAME = "mochitest3-track-simple";
const SOCIAL_ANNOTATION_TABLE_PREF =
  "urlclassifier.features.socialtracking.annotate.blacklistTables";
const SOCIAL_TRACKING_TABLE_NAME = "mochitest4-track-simple";
const SOCIAL_TRACKING_TABLE_PREF =
  "urlclassifier.features.socialtracking.blacklistTables";
const EMAIL_TRACKING_TABLE_NAME = "mochitest5-track-simple";
const EMAIL_TRACKING_TABLE_PREF =
  "urlclassifier.features.emailtracking.blocklistTables";
const CONSENTMANAGER_ANNOTATION_TABLE_NAME = "mochitest6-track-simple";
const CONSENTMANAGER_ANNOTATION_TABLE_PREF =
  "urlclassifier.features.consentmanager.annotate.blocklistTables";
const ANTIFRAUD_ANNOTATION_TABLE_NAME = "mochitest7-track-simple";
const ANTIFRAUD_ANNOTATION_TABLE_PREF =
  "urlclassifier.features.antifraud.annotate.blocklistTables";

const PROVIDER_V5_LISTS_PREF = "browser.safebrowsing.provider.google5.lists";
const MALWARE_TABLE_PREF = "urlclassifier.malwareTable";
const MALWARE_V5_TABLE_NAME = "test-google5-malware-proto";
const MALWARE_HOST = "malware.example.com/";

const GLOBALCACHE_TABLE_PREF = "urlclassifier.globalCacheTable";
const GLOBALCACHE_V5_TABLE_NAME = "test-globalcache-proto";
const GLOBALCACHE_HOST = "globalcache-test.example.com/";

const SKIP_URLCLASSIFIER_PREF =
  "privacy.trackingprotection.urlclassifier.disable_for_channel_classifier";

let timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ContentClassifierTestUtils:
    "resource://testing-common/ContentClassifierTestUtils.sys.mjs",
});

// The content classifier only stands in for url-classifier's tracking
// protection features once it is the engine actually consulted for channel
// classification. While url-classifier still runs, its own tables are the ones
// under test and equivalent content classifier lists would have no effect.
function contentClassifierInUse() {
  return Services.prefs.getBoolPref(SKIP_URLCLASSIFIER_PREF, false);
}

// Whether lists were handed to ContentClassifierTestUtils, so that
// cleanupTestTrackers() takes them down even if the content classifier is no
// longer in use by then.
let gContentClassifierListsPublished = false;

// url-classifier prefs that tests seed tracker hosts through, keyed by the
// content classifier engine covering the same feature.
const HOST_PREFS_BY_ENGINE = {
  test_block: ["urlclassifier.trackingTable.testEntries"],
  test_annotate: ["urlclassifier.trackingAnnotationTable.testEntries"],
  cryptominers: [
    "urlclassifier.features.cryptomining.blacklistHosts",
    "urlclassifier.features.cryptomining.annotate.blacklistHosts",
  ],
  fingerprinters: [
    "urlclassifier.features.fingerprinting.blacklistHosts",
    "urlclassifier.features.fingerprinting.annotate.blacklistHosts",
  ],
  "social-trackers": [
    "urlclassifier.features.socialtracking.blacklistHosts",
    "urlclassifier.features.socialtracking.annotate.blacklistHosts",
  ],
  "email-trackers": ["urlclassifier.features.emailtracking.blocklistHosts"],
  "harmful-addon": ["urlclassifier.features.harmfuladdon.blocklistHosts"],
};

// Hosts url-classifier's tracking tables carry as built-in test entries (see
// LookupCacheV2::LoadMozEntries), which the content classifier lacks.
const BUILT_IN_TRACKER_HOSTS = ["trackertest.org", "itisatracker.org"];

// url-classifier skips the URLs matching these prefs, for tracking protection
// and tracking annotation respectively. Only host-only patterns like
// "*://host/*" or "*://*.host/*" are understood.
const TRACKING_SKIP_URLS_PREF = "urlclassifier.trackingSkipURLs";
const TRACKING_ANNOTATION_SKIP_URLS_PREF =
  "urlclassifier.trackingAnnotationSkipURLs";

function skipHostsFromPref(pref) {
  // url-classifier ignores its skip lists while webcompat is disabled.
  if (
    !Services.prefs.getBoolPref("privacy.antitracking.enableWebcompat", true)
  ) {
    return [];
  }
  let hosts = new Set();
  for (let pattern of Services.prefs.getStringPref(pref, "").split(",")) {
    let match = /^\*:\/\/(?:\*\.)?([^/*]+)\/\*$/.exec(pattern.trim());
    if (match) {
      hosts.add(match[1]);
    }
  }
  return Array.from(hosts);
}

function setContentClassifierSkipHosts() {
  lazy.ContentClassifierTestUtils.setSkipHosts(
    skipHostsFromPref(TRACKING_SKIP_URLS_PREF),
    skipHostsFromPref(TRACKING_ANNOTATION_SKIP_URLS_PREF)
  );
}

export var UrlClassifierTestUtils = {
  async addTestTrackers() {
    // Add some URLs to the tracking databases
    let annotationURL1 = "tracking.example.org/"; // only for annotations
    let annotationURL2 = "itisatracker.org/";
    let annotationURL3 = "trackertest.org/";
    let annotationURL4 = "another-tracking.example.net/";
    let annotationURL5 = "tlsresumptiontest.example.org/";
    let annotationEntitylistedURL = "itisatrap.org/?resource=example.org";
    let trackingURL1 = "tracking.example.com/"; // only for TP
    let trackingURL2 = "itisatracker.org/";
    let trackingURL3 = "trackertest.org/";
    let entitylistedURL = "itisatrap.org/?resource=itisatracker.org";
    let socialTrackingURL = "social-tracking.example.org/";
    let emailTrackingURL = "email-tracking.example.org/";
    let consentmanagerAnnotationURL = "consent-manager.example.org/";
    let antifraudAnnotationURL = "anti-fraud.example.org/";

    let annotationUpdate =
      "n:1000\ni:" +
      ANNOTATION_TABLE_NAME +
      "\nad:5\n" +
      "a:1:32:" +
      annotationURL1.length +
      "\n" +
      annotationURL1 +
      "\n" +
      "a:2:32:" +
      annotationURL2.length +
      "\n" +
      annotationURL2 +
      "\n" +
      "a:3:32:" +
      annotationURL3.length +
      "\n" +
      annotationURL3 +
      "\n" +
      "a:4:32:" +
      annotationURL4.length +
      "\n" +
      annotationURL4 +
      "\n" +
      "a:5:32:" +
      annotationURL5.length +
      "\n" +
      annotationURL5 +
      "\n" +
      "a:6:32:" +
      consentmanagerAnnotationURL.length +
      "\n" +
      consentmanagerAnnotationURL +
      "\n" +
      "a:7:32:" +
      antifraudAnnotationURL.length +
      "\n" +
      antifraudAnnotationURL +
      "\n";
    let socialAnnotationUpdate =
      "n:1000\ni:" +
      SOCIAL_ANNOTATION_TABLE_NAME +
      "\nad:1\n" +
      "a:1:32:" +
      socialTrackingURL.length +
      "\n" +
      socialTrackingURL +
      "\n";
    let consentmanagerAnnotationUpdate =
      "n:1000\ni:" +
      CONSENTMANAGER_ANNOTATION_TABLE_NAME +
      "\nad:1\n" +
      "a:1:32:" +
      consentmanagerAnnotationURL.length +
      "\n" +
      consentmanagerAnnotationURL +
      "\n";
    let antifraudAnnotationUpdate =
      "n:1000\ni:" +
      ANTIFRAUD_ANNOTATION_TABLE_NAME +
      "\nad:1\n" +
      "a:1:32:" +
      antifraudAnnotationURL.length +
      "\n" +
      antifraudAnnotationURL +
      "\n";
    let annotationEntitylistUpdate =
      "n:1000\ni:" +
      ANNOTATION_ENTITYLIST_TABLE_NAME +
      "\nad:1\n" +
      "a:1:32:" +
      annotationEntitylistedURL.length +
      "\n" +
      annotationEntitylistedURL +
      "\n";
    let trackingUpdate =
      "n:1000\ni:" +
      TRACKING_TABLE_NAME +
      "\nad:3\n" +
      "a:1:32:" +
      trackingURL1.length +
      "\n" +
      trackingURL1 +
      "\n" +
      "a:2:32:" +
      trackingURL2.length +
      "\n" +
      trackingURL2 +
      "\n" +
      "a:3:32:" +
      trackingURL3.length +
      "\n" +
      trackingURL3 +
      "\n" +
      "a:4:32:" +
      consentmanagerAnnotationURL.length +
      "\n" +
      consentmanagerAnnotationURL +
      "\n" +
      "a:5:32:" +
      antifraudAnnotationURL.length +
      "\n" +
      antifraudAnnotationURL +
      "\n";
    let socialTrackingUpdate =
      "n:1000\ni:" +
      SOCIAL_TRACKING_TABLE_NAME +
      "\nad:1\n" +
      "a:1:32:" +
      socialTrackingURL.length +
      "\n" +
      socialTrackingURL +
      "\n";
    let emailTrackingUpdate =
      "n:1000\ni:" +
      EMAIL_TRACKING_TABLE_NAME +
      "\nad:1\n" +
      "a:1:32:" +
      emailTrackingURL.length +
      "\n" +
      emailTrackingURL +
      "\n";
    let entitylistUpdate =
      "n:1000\ni:" +
      ENTITYLIST_TABLE_NAME +
      "\nad:1\n" +
      "a:1:32:" +
      entitylistedURL.length +
      "\n" +
      entitylistedURL +
      "\n";

    var tables = [
      {
        pref: ANNOTATION_TABLE_PREF,
        name: ANNOTATION_TABLE_NAME,
        update: annotationUpdate,
      },
      {
        pref: SOCIAL_ANNOTATION_TABLE_PREF,
        name: SOCIAL_ANNOTATION_TABLE_NAME,
        update: socialAnnotationUpdate,
      },
      {
        pref: CONSENTMANAGER_ANNOTATION_TABLE_PREF,
        name: CONSENTMANAGER_ANNOTATION_TABLE_NAME,
        update: consentmanagerAnnotationUpdate,
      },
      {
        pref: ANTIFRAUD_ANNOTATION_TABLE_PREF,
        name: ANTIFRAUD_ANNOTATION_TABLE_NAME,
        update: antifraudAnnotationUpdate,
      },
      {
        pref: ANNOTATION_ENTITYLIST_TABLE_PREF,
        name: ANNOTATION_ENTITYLIST_TABLE_NAME,
        update: annotationEntitylistUpdate,
      },
      {
        pref: TRACKING_TABLE_PREF,
        name: TRACKING_TABLE_NAME,
        update: trackingUpdate,
      },
      {
        pref: SOCIAL_TRACKING_TABLE_PREF,
        name: SOCIAL_TRACKING_TABLE_NAME,
        update: socialTrackingUpdate,
      },
      {
        pref: EMAIL_TRACKING_TABLE_PREF,
        name: EMAIL_TRACKING_TABLE_NAME,
        update: emailTrackingUpdate,
      },
      {
        pref: ENTITYLIST_TABLE_PREF,
        name: ENTITYLIST_TABLE_NAME,
        update: entitylistUpdate,
      },
    ];

    let tableIndex = 0;
    let doOneUpdate = () => {
      if (tableIndex == tables.length) {
        return Promise.resolve();
      }
      return this.useTestDatabase(tables[tableIndex]).then(
        () => {
          tableIndex++;
          return doOneUpdate();
        },
        aErrMsg => {
          dump("Rejected: " + aErrMsg + ". Retry later.\n");
          return new Promise(resolve => {
            timer.initWithCallback(resolve, 100, Ci.nsITimer.TYPE_ONE_SHOT);
          }).then(doOneUpdate);
        }
      );
    };

    await doOneUpdate();

    if (contentClassifierInUse()) {
      gContentClassifierListsPublished = true;
      setContentClassifierSkipHosts();
      await lazy.ContentClassifierTestUtils.addTestTrackers();
    }
  },

  /**
   * Publish the hosts currently set in the url-classifier host prefs (see
   * HOST_PREFS_BY_ENGINE) as content classifier lists, so that a test seeding
   * trackers through those prefs keeps working once the content classifier is
   * the channel classifier. Call it after setting the prefs; it is a no-op
   * while url-classifier is the channel classifier. cleanupTestTrackers()
   * drops the published lists again.
   */
  async addTestHostsFromPrefs() {
    if (!contentClassifierInUse()) {
      return;
    }
    gContentClassifierListsPublished = true;
    setContentClassifierSkipHosts();
    let hostsByEngine = {};
    for (let [engine, prefs] of Object.entries(HOST_PREFS_BY_ENGINE)) {
      let hosts = prefs
        .flatMap(pref => Services.prefs.getStringPref(pref, "").split(","))
        .map(host => host.trim())
        .filter(host => host);
      if (hosts.length) {
        hostsByEngine[engine] = hosts;
      }
    }
    await lazy.ContentClassifierTestUtils.addTestHosts(hostsByEngine);
  },

  /**
   * Give the content classifier the tracker hosts url-classifier classifies
   * out of the box, for tests that rely on them without seeding anything.
   * A no-op while url-classifier is the channel classifier.
   * cleanupTestTrackers() drops them again.
   */
  async addBuiltInTestTrackers() {
    if (!contentClassifierInUse()) {
      return;
    }
    gContentClassifierListsPublished = true;
    setContentClassifierSkipHosts();
    await lazy.ContentClassifierTestUtils.addTestHosts({
      test_block: BUILT_IN_TRACKER_HOSTS,
      test_annotate: BUILT_IN_TRACKER_HOSTS,
    });
  },

  async cleanupTestTrackers() {
    Services.prefs.clearUserPref(ANNOTATION_TABLE_PREF);
    Services.prefs.clearUserPref(SOCIAL_ANNOTATION_TABLE_PREF);
    Services.prefs.clearUserPref(CONSENTMANAGER_ANNOTATION_TABLE_PREF);
    Services.prefs.clearUserPref(ANTIFRAUD_ANNOTATION_TABLE_PREF);
    Services.prefs.clearUserPref(ANNOTATION_ENTITYLIST_TABLE_PREF);
    Services.prefs.clearUserPref(TRACKING_TABLE_PREF);
    Services.prefs.clearUserPref(SOCIAL_TRACKING_TABLE_PREF);
    Services.prefs.clearUserPref(EMAIL_TRACKING_TABLE_PREF);
    Services.prefs.clearUserPref(ENTITYLIST_TABLE_PREF);

    if (gContentClassifierListsPublished) {
      gContentClassifierListsPublished = false;
      await lazy.ContentClassifierTestUtils.cleanupTestTrackers();
    }
  },

  /**
   * Add test entries to V5 SafeBrowsing tables.
   * Adds entries to both the malware table (4-byte hash) and the globalcache
   * table (32-byte hash).
   *
   * @param {string} [globalCacheHost] - The host for the globalcache table entry.
   *                                     Defaults to GLOBALCACHE_HOST.
   * @returns {Promise}
   */
  async addTestV5Entry(globalCacheHost = GLOBALCACHE_HOST) {
    let urlClassifierTestUtils = Cc[
      "@mozilla.org/url-classifier/test-utils;1"
    ].getService(Ci.nsIUrlClassifierTestUtils);

    // Generate hashes for both tables.
    let lookupHash = urlClassifierTestUtils.generateLookupHash(MALWARE_HOST);
    let fullHash = urlClassifierTestUtils.generateFullHashRaw(globalCacheHost);

    let updateData4b = urlClassifierTestUtils.makeUpdateResponseV5(
      "test",
      lookupHash
    );
    let updateData32b = urlClassifierTestUtils.makeUpdateResponseV5_32b(
      "test",
      fullHash
    );

    // Set up the provider lists and table prefs.
    let tableNames = MALWARE_V5_TABLE_NAME + "," + GLOBALCACHE_V5_TABLE_NAME;
    Services.prefs.setCharPref(PROVIDER_V5_LISTS_PREF, tableNames);
    Services.prefs.setCharPref(MALWARE_TABLE_PREF, MALWARE_V5_TABLE_NAME);
    Services.prefs.setCharPref(
      GLOBALCACHE_TABLE_PREF,
      GLOBALCACHE_V5_TABLE_NAME
    );

    // Update malware table (4-byte hash).
    await this.doV5Update(MALWARE_V5_TABLE_NAME, updateData4b);

    // Update globalcache table (32-byte hash).
    await this.doV5Update(GLOBALCACHE_V5_TABLE_NAME, updateData32b);
  },

  doV5Update(tableName, updateData) {
    return new Promise((resolve, reject) => {
      let dbService = Cc["@mozilla.org/url-classifier/dbservice;1"].getService(
        Ci.nsIUrlClassifierDBService
      );
      let listener = {
        QueryInterface: iid => {
          if (
            iid.equals(Ci.nsISupports) ||
            iid.equals(Ci.nsIUrlClassifierUpdateObserver)
          ) {
            return listener;
          }

          throw Components.Exception("", Cr.NS_ERROR_NO_INTERFACE);
        },
        updateUrlRequested: () => {},
        streamFinished: () => {},
        updateError: () => {
          reject("Got updateError when updating " + tableName);
        },
        updateSuccess: () => {
          resolve();
        },
      };

      try {
        dbService.beginUpdate(listener, tableName, "google5");
        dbService.beginStream("", "");
        dbService.updateStream(updateData);
        dbService.finishStream();
        dbService.finishUpdate();
      } catch (e) {
        reject("Failed to update " + tableName + ": " + e);
      }
    });
  },

  cleanupTestV5Entry() {
    Services.prefs.clearUserPref(PROVIDER_V5_LISTS_PREF);
    Services.prefs.clearUserPref(MALWARE_TABLE_PREF);
    Services.prefs.clearUserPref(GLOBALCACHE_TABLE_PREF);
  },

  /**
   * Add some entries to a test tracking protection database, and resets
   * back to the default database after the test ends.
   *
   * @return {Promise}
   */
  useTestDatabase(table) {
    Services.prefs.setCharPref(table.pref, table.name);

    return new Promise((resolve, reject) => {
      let dbService = Cc["@mozilla.org/url-classifier/dbservice;1"].getService(
        Ci.nsIUrlClassifierDBService
      );
      let listener = {
        QueryInterface: iid => {
          if (
            iid.equals(Ci.nsISupports) ||
            iid.equals(Ci.nsIUrlClassifierUpdateObserver)
          ) {
            return listener;
          }

          throw Components.Exception("", Cr.NS_ERROR_NO_INTERFACE);
        },
        updateUrlRequested: () => {},
        streamFinished: () => {},
        updateError: () => {
          reject("Got updateError when updating " + table.name);
        },
        updateSuccess: () => {
          resolve();
        },
      };

      try {
        dbService.beginUpdate(listener, table.name, "");
        dbService.beginStream("", "");
        dbService.updateStream(table.update);
        dbService.finishStream();
        dbService.finishUpdate();
      } catch (e) {
        reject("Failed to update with dbService: " + table.name);
      }
    });
  },

  /**
   * Handle the next "urlclassifier-before-block-channel" event.
   *
   * @param {object} options
   * @param {string} [options.filterOrigin] - Only handle event for channels
   * with matching origin.
   * @param {function} [options.onBeforeBlockChannel] - Optional callback for
   * the event. Called before acting on the channel.
   * @param {("allow"|"replace")} [options.action] - Whether to allow or replace
   * the channel.
   * @returns {Promise} - Resolves once event has been handled.
   */
  handleBeforeBlockChannel({
    filterOrigin = null,
    onBeforeBlockChannel,
    action,
  }) {
    if (action && action != "allow" && action != "replace") {
      throw new Error("Invalid action " + action);
    }
    let channelClassifierService = Cc[
      "@mozilla.org/url-classifier/channel-classifier-service;1"
    ].getService(Ci.nsIChannelClassifierService);

    let resolver;
    let promise = new Promise(resolve => {
      resolver = resolve;
    });

    let observer = {
      observe(subject, topic) {
        if (topic != "urlclassifier-before-block-channel") {
          return;
        }
        let channel = subject.QueryInterface(Ci.nsIUrlClassifierBlockedChannel);

        if (filterOrigin) {
          let { url } = channel;
          let { origin } = new URL(url);
          if (filterOrigin != origin) {
            return;
          }
        }

        if (onBeforeBlockChannel) {
          onBeforeBlockChannel(channel);
        }
        if (action) {
          channel[action]();
        }

        channelClassifierService.removeListener(observer);
        resolver();
      },
    };
    channelClassifierService.addListener(observer);
    return promise;
  },

  cleanRealTimeSimulatorCache() {
    let dbService = Cc["@mozilla.org/url-classifier/dbservice;1"].getService(
      Ci.nsIUrlClassifierDBService
    );
    dbService.cleanRealTimeSimulatorCache();
  },

  expireRealTimeSimulatorCache() {
    let dbService = Cc["@mozilla.org/url-classifier/dbservice;1"].getService(
      Ci.nsIUrlClassifierDBService
    );
    dbService.expireRealTimeSimulatorCache();
  },
};
