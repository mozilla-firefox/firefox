/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  ContentPref,
  cbHandleCompletion,
  cbHandleError,
  cbHandleResult,
} from "moz-src:///toolkit/components/contentprefs/ContentPrefUtils.sys.mjs";

import { ContentPrefStore } from "moz-src:///toolkit/components/contentprefs/ContentPrefStore.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  Sqlite: "resource://gre/modules/Sqlite.sys.mjs",
});

const CACHE_MAX_GROUP_ENTRIES = 100;

const GROUP_CLAUSE = `
  SELECT id
  FROM groups
  WHERE name = :group OR
        (:includeSubdomains AND name LIKE :pattern ESCAPE '/')
`;

export function ContentPrefService2() {
  if (Services.appinfo.processType === Services.appinfo.PROCESS_TYPE_CONTENT) {
    return ChromeUtils.importESModule(
      "moz-src:///toolkit/components/contentprefs/ContentPrefServiceChild.sys.mjs"
    ).ContentPrefServiceChild;
  }

  Services.obs.addObserver(this, "last-pb-context-exited");

  // Observe shutdown so we can shut down the database connection.
  Services.obs.addObserver(this, "profile-before-change");
}

const cache = new ContentPrefStore();
cache.set = function CPS_cache_set() {
  Object.getPrototypeOf(this).set.apply(this, arguments);
  let groupCount = this._groups.size;
  if (groupCount >= CACHE_MAX_GROUP_ENTRIES) {
    // Clean half of the entries
    for (let [group, name] of this) {
      this.remove(group, name);
      groupCount--;
      if (groupCount < CACHE_MAX_GROUP_ENTRIES / 2) {
        break;
      }
    }
  }
};

const privModeStorage = new ContentPrefStore();

function executeStatementsInTransaction(conn, stmts) {
  return conn.executeTransaction(async () => {
    let rows = [];
    for (let { sql, params, cachable } of stmts) {
      let execute = cachable ? conn.executeCached : conn.execute;
      let stmtRows = await execute.call(conn, sql, params);
      rows = rows.concat(stmtRows);
    }
    return rows;
  });
}

/**
 * Helper function to extract a non-empty group from a URI.
 *
 * @param {nsIURI} uri The URI to extract from.
 * @returns {string} a non-empty group.
 * @throws if a non-empty group cannot be extracted.
 */
function nonEmptyGroupFromURI(uri) {
  if (uri.schemeIs("blob")) {
    // blob: URLs are generated internally for a specific browser instance,
    // thus storing settings for them would be pointless. Though in most cases
    // it's possible to extract an origin from them and use it as the group.
    let embeddedURL = new URL(URL.fromURI(uri).origin);
    if (/^https?:$/.test(embeddedURL.protocol)) {
      return embeddedURL.host;
    }
    if (embeddedURL.origin) {
      // Keep the protocol if it's not http(s), to avoid mixing up settings
      // for different protocols, e.g. resource://moz.com and https://moz.com.
      return embeddedURL.origin;
    }
  }
  if (uri.host) {
    // Accessing the host property of the URI will throw an exception
    // if the URI is of a type that doesn't have a host property.
    // Otherwise, we manually throw an exception if the host is empty,
    // since the effect is the same (we can't derive a group from it).
    return uri.host;
  }
  // If reach this point, we'd have an empty group.
  throw new Error(`Can't derive non-empty CPS group from ${uri.spec}`);
}

function HostnameGrouper_group(aURI) {
  try {
    return nonEmptyGroupFromURI(aURI);
  } catch (ex) {
    // If we don't have a host, then use the entire URI (minus the query,
    // reference, and hash, if possible) as the group.  This means that URIs
    // like about:mozilla and about:blank will be considered separate groups,
    // but at least they'll be grouped somehow.

    // This also means that each individual file: URL will be considered
    // its own group.  This seems suboptimal, but so does treating the entire
    // file: URL space as a single group (especially if folks start setting
    // group-specific capabilities prefs).

    // XXX Is there something better we can do here?

    try {
      return aURI.prePath + aURI.filePath;
    } catch (ex) {
      return aURI.spec;
    }
  }
}

ContentPrefService2.prototype = {
  // XPCOM Plumbing

  classID: Components.ID("{e3f772f3-023f-4b32-b074-36cf0fd5d414}"),

  // Destruction

  _destroy() {
    Services.obs.removeObserver(this, "profile-before-change");
    Services.obs.removeObserver(this, "last-pb-context-exited");

    // Delete references to XPCOM components to make sure we don't leak them
    // (although we haven't observed leakage in tests).  Also delete references
    // in _observers and _genericObservers to avoid cycles with those that
    // refer to us and don't remove themselves from those observer pools.
    delete this._observers;
    delete this._genericObservers;
  },

  // in-memory cache and private-browsing stores

  _cache: cache,
  _pbStore: privModeStorage,

  _connPromise: null,

  get conn() {
    if (this._connPromise) {
      return this._connPromise;
    }

    // Register the blocker before opening: a connection that is still opening
    // when Sqlite.shutdown begins must still be closed before the barrier
    // completes.
    try {
      lazy.Sqlite.shutdown.addBlocker(
        "Closing ContentPrefService2 connection.",
        async () => {
          let conn = await this._connPromise?.catch(() => null);
          await conn?.close();
        }
      );
    } catch (e) {
      // Nothing would close the connection, so don't open one.
      return (this._connPromise = Promise.resolve(null));
    }

    return (this._connPromise = (async () => {
      let conn;
      try {
        conn = await this._getConnection();
      } catch (e) {
        this.log("Failed to establish database connection: " + e);
        throw e;
      }
      return conn;
    })());
  },

  // nsIContentPrefService

  getByName: function CPS2_getByName(name, context, callback) {
    checkNameArg(name);
    checkCallbackArg(callback, true);

    // Some prefs may be in both the database and the private browsing store.
    // Notify the caller of such prefs only once, using the values from private
    // browsing.
    let pbPrefs = new ContentPrefStore();
    if (context && context.usePrivateBrowsing) {
      for (let [sgroup, sname, val] of this._pbStore) {
        if (sname == name) {
          pbPrefs.set(sgroup, sname, val);
        }
      }
    }

    let stmt1 = this._stmt(`
      SELECT groups.name AS grp, prefs.value AS value
      FROM prefs
      JOIN settings ON settings.id = prefs.settingID
      JOIN groups ON groups.id = prefs.groupID
      WHERE settings.name = :name
    `);
    stmt1.params.name = name;

    let stmt2 = this._stmt(`
      SELECT NULL AS grp, prefs.value AS value
      FROM prefs
      JOIN settings ON settings.id = prefs.settingID
      WHERE settings.name = :name AND prefs.groupID ISNULL
    `);
    stmt2.params.name = name;

    this._execStmts([stmt1, stmt2], {
      onRow: row => {
        let grp = row.getResultByName("grp");
        let val = row.getResultByName("value");
        this._cache.set(grp, name, val);
        if (!pbPrefs.has(grp, name)) {
          cbHandleResult(callback, new ContentPref(grp, name, val));
        }
      },
      onDone: (reason, ok) => {
        if (ok) {
          for (let [pbGroup, pbName, pbVal] of pbPrefs) {
            cbHandleResult(callback, new ContentPref(pbGroup, pbName, pbVal));
          }
        }
        cbHandleCompletion(callback, reason);
      },
      onError: nsresult => {
        cbHandleError(callback, nsresult);
      },
    });
  },

  getByDomainAndName: function CPS2_getByDomainAndName(
    group,
    name,
    context,
    callback
  ) {
    checkGroupArg(group);
    this._get(group, name, false, context, callback);
  },

  getBySubdomainAndName: function CPS2_getBySubdomainAndName(
    group,
    name,
    context,
    callback
  ) {
    checkGroupArg(group);
    this._get(group, name, true, context, callback);
  },

  getGlobal: function CPS2_getGlobal(name, context, callback) {
    this._get(null, name, false, context, callback);
  },

  _get: function CPS2__get(group, name, includeSubdomains, context, callback) {
    group = this._parseGroup(group);
    checkNameArg(name);
    checkCallbackArg(callback, true);

    // Some prefs may be in both the database and the private browsing store.
    // Notify the caller of such prefs only once, using the values from private
    // browsing.
    let pbPrefs = new ContentPrefStore();
    if (context && context.usePrivateBrowsing) {
      for (let [sgroup, val] of this._pbStore.match(
        group,
        name,
        includeSubdomains
      )) {
        pbPrefs.set(sgroup, name, val);
      }
    }

    this._execStmts([this._commonGetStmt(group, name, includeSubdomains)], {
      onRow: row => {
        let grp = row.getResultByName("grp");
        let val = row.getResultByName("value");
        this._cache.set(grp, name, val);
        if (!pbPrefs.has(group, name)) {
          cbHandleResult(callback, new ContentPref(grp, name, val));
        }
      },
      onDone: (reason, ok, gotRow) => {
        if (ok) {
          if (!gotRow) {
            this._cache.set(group, name, undefined);
          }
          for (let [pbGroup, pbName, pbVal] of pbPrefs) {
            cbHandleResult(callback, new ContentPref(pbGroup, pbName, pbVal));
          }
        }
        cbHandleCompletion(callback, reason);
      },
      onError: nsresult => {
        cbHandleError(callback, nsresult);
      },
    });
  },

  _commonGetStmt: function CPS2__commonGetStmt(group, name, includeSubdomains) {
    let stmt = group
      ? this._stmtWithGroupClause(
          group,
          includeSubdomains,
          `
        SELECT groups.name AS grp, prefs.value AS value
        FROM prefs
        JOIN settings ON settings.id = prefs.settingID
        JOIN groups ON groups.id = prefs.groupID
        WHERE settings.name = :name AND prefs.groupID IN (${GROUP_CLAUSE})
      `
        )
      : this._stmt(`
        SELECT NULL AS grp, prefs.value AS value
        FROM prefs
        JOIN settings ON settings.id = prefs.settingID
        WHERE settings.name = :name AND prefs.groupID ISNULL
      `);
    stmt.params.name = name;
    return stmt;
  },

  _stmtWithGroupClause: function CPS2__stmtWithGroupClause(
    group,
    includeSubdomains,
    sql
  ) {
    let stmt = this._stmt(sql, false);
    stmt.params.group = group;
    stmt.params.includeSubdomains = includeSubdomains || false;
    stmt.params.pattern =
      "%." + (group == null ? null : group.replace(/\/|%|_/g, "/$&"));
    return stmt;
  },

  getCachedByDomainAndName: function CPS2_getCachedByDomainAndName(
    group,
    name,
    context
  ) {
    checkGroupArg(group);
    let prefs = this._getCached(group, name, false, context);
    return prefs[0] || null;
  },

  getCachedBySubdomainAndName: function CPS2_getCachedBySubdomainAndName(
    group,
    name,
    context
  ) {
    checkGroupArg(group);
    return this._getCached(group, name, true, context);
  },

  getCachedGlobal: function CPS2_getCachedGlobal(name, context) {
    let prefs = this._getCached(null, name, false, context);
    return prefs[0] || null;
  },

  _getCached: function CPS2__getCached(
    group,
    name,
    includeSubdomains,
    context
  ) {
    group = this._parseGroup(group);
    checkNameArg(name);

    let storesToCheck = [this._cache];
    if (context && context.usePrivateBrowsing) {
      storesToCheck.push(this._pbStore);
    }

    let outStore = new ContentPrefStore();
    storesToCheck.forEach(function (store) {
      for (let [sgroup, val] of store.match(group, name, includeSubdomains)) {
        outStore.set(sgroup, name, val);
      }
    });

    let prefs = [];
    for (let [sgroup, sname, val] of outStore) {
      prefs.push(new ContentPref(sgroup, sname, val));
    }
    return prefs;
  },

  set: function CPS2_set(group, name, value, context, callback) {
    checkGroupArg(group);
    this._set(group, name, value, context, callback);
  },

  setGlobal: function CPS2_setGlobal(name, value, context, callback) {
    this._set(null, name, value, context, callback);
  },

  _set: function CPS2__set(group, name, value, context, callback) {
    group = this._parseGroup(group);
    checkNameArg(name);
    checkValueArg(value);
    checkCallbackArg(callback, false);

    if (context && context.usePrivateBrowsing) {
      this._pbStore.set(group, name, value);
      this._schedule(function () {
        cbHandleCompletion(callback, Ci.nsIContentPrefCallback2.COMPLETE_OK);
        this._notifyPrefSet(group, name, value, context.usePrivateBrowsing);
      });
      return;
    }

    // Invalidate the cached value so consumers accessing the cache between now
    // and when the operation finishes don't get old data.
    this._cache.remove(group, name);

    let stmts = [];

    // Create the setting if it doesn't exist.
    let stmt = this._stmt(`
      INSERT OR IGNORE INTO settings (id, name)
      VALUES((SELECT id FROM settings WHERE name = :name), :name)
    `);
    stmt.params.name = name;
    stmts.push(stmt);

    // Create the group if it doesn't exist.
    if (group) {
      stmt = this._stmt(`
        INSERT OR IGNORE INTO groups (id, name)
        VALUES((SELECT id FROM groups WHERE name = :group), :group)
      `);
      stmt.params.group = group;
      stmts.push(stmt);
    }

    // Finally create or update the pref.
    if (group) {
      stmt = this._stmt(`
        INSERT OR REPLACE INTO prefs (id, groupID, settingID, value, timestamp)
        VALUES(
          (SELECT prefs.id
           FROM prefs
           JOIN groups ON groups.id = prefs.groupID
           JOIN settings ON settings.id = prefs.settingID
           WHERE groups.name = :group AND settings.name = :name),
          (SELECT id FROM groups WHERE name = :group),
          (SELECT id FROM settings WHERE name = :name),
          :value,
          :now
        )
      `);
      stmt.params.group = group;
    } else {
      stmt = this._stmt(`
        INSERT OR REPLACE INTO prefs (id, groupID, settingID, value, timestamp)
        VALUES(
          (SELECT prefs.id
           FROM prefs
           JOIN settings ON settings.id = prefs.settingID
           WHERE prefs.groupID IS NULL AND settings.name = :name),
          NULL,
          (SELECT id FROM settings WHERE name = :name),
          :value,
          :now
        )
      `);
    }
    stmt.params.name = name;
    stmt.params.value = value;
    stmt.params.now = Date.now() / 1000;
    stmts.push(stmt);

    this._execStmts(stmts, {
      onDone: (reason, ok) => {
        if (ok) {
          this._cache.setWithCast(group, name, value);
        }
        cbHandleCompletion(callback, reason);
        if (ok) {
          this._notifyPrefSet(
            group,
            name,
            value,
            context && context.usePrivateBrowsing
          );
        }
      },
      onError: nsresult => {
        cbHandleError(callback, nsresult);
      },
    });
  },

  removeByDomainAndName: function CPS2_removeByDomainAndName(
    group,
    name,
    context,
    callback
  ) {
    checkGroupArg(group);
    this._remove(group, name, false, context, callback);
  },

  removeBySubdomainAndName: function CPS2_removeBySubdomainAndName(
    group,
    name,
    context,
    callback
  ) {
    checkGroupArg(group);
    this._remove(group, name, true, context, callback);
  },

  removeGlobal: function CPS2_removeGlobal(name, context, callback) {
    this._remove(null, name, false, context, callback);
  },

  _remove: function CPS2__remove(
    group,
    name,
    includeSubdomains,
    context,
    callback
  ) {
    group = this._parseGroup(group);
    checkNameArg(name);
    checkCallbackArg(callback, false);

    // Invalidate the cached values so consumers accessing the cache between now
    // and when the operation finishes don't get old data.
    for (let sgroup of this._cache.matchGroups(group, includeSubdomains)) {
      this._cache.remove(sgroup, name);
    }

    let isPrivate = context && context.usePrivateBrowsing;
    let stmts = [];

    if (context == null || !isPrivate) {
      // First get the matching prefs.
      stmts.push(this._commonGetStmt(group, name, includeSubdomains));

      // Delete the matching prefs.
      let stmt = this._stmtWithGroupClause(
        group,
        includeSubdomains,
        `
      DELETE FROM prefs
      WHERE settingID = (SELECT id FROM settings WHERE name = :name) AND
            CASE typeof(:group)
            WHEN 'null' THEN prefs.groupID IS NULL
            ELSE prefs.groupID IN (${GROUP_CLAUSE})
            END
    `
      );
      stmt.params.name = name;
      stmts.push(stmt);

      stmts = stmts.concat(this._settingsAndGroupsCleanupStmts());
    }

    let prefsNonPrivateBrowsing = new ContentPrefStore();

    // Only execute if we have statements to run. stmts is empty for clearing
    // only private browsing data.
    let queryPromise = Promise.resolve([
      Ci.nsIContentPrefCallback2.COMPLETE_OK,
      false,
    ]);

    if (stmts.length) {
      queryPromise = new Promise(resolve => {
        this._execStmts(stmts, {
          onRow: row => {
            let grp = row.getResultByName("grp");
            prefsNonPrivateBrowsing.set(grp, name, undefined);
            this._cache.set(grp, name, undefined);
          },
          onDone: (reason, ok) => {
            this._cache.set(group, name, undefined);
            resolve([reason, ok]);
          },
          onError: nsresult => {
            cbHandleError(callback, nsresult);
          },
        });
      });
    }

    let prefsPrivateBrowsing = new ContentPrefStore();

    if (isPrivate || context == null) {
      for (let [sgroup] of this._pbStore.match(
        group,
        name,
        includeSubdomains
      )) {
        prefsPrivateBrowsing.set(sgroup, name, undefined);
        this._pbStore.remove(sgroup, name);
      }
    }

    queryPromise.then(([reason, hasNonPrivateEntries]) => {
      // Notify caller of completion.
      cbHandleCompletion(callback, reason);

      // Notify for non PBM changes.
      if (hasNonPrivateEntries) {
        for (let [sgroup] of prefsNonPrivateBrowsing) {
          this._notifyPrefRemoved(sgroup, name, false);
        }
      }

      // Notify for PBM changes.
      for (let [sgroup] of prefsPrivateBrowsing) {
        this._notifyPrefRemoved(sgroup, name, true);
      }
    });
  },

  // Deletes settings and groups that are no longer used.
  _settingsAndGroupsCleanupStmts() {
    // The NOTNULL term in the subquery of the second statment is needed because of
    // SQLite's weird IN behavior vis-a-vis NULLs.  See http://sqlite.org/lang_expr.html.
    return [
      this._stmt(`
        DELETE FROM settings
        WHERE id NOT IN (SELECT DISTINCT settingID FROM prefs)
      `),
      this._stmt(`
        DELETE FROM groups WHERE id NOT IN (
          SELECT DISTINCT groupID FROM prefs WHERE groupID NOTNULL
        )
      `),
    ];
  },

  removeByDomain: function CPS2_removeByDomain(group, context, callback) {
    checkGroupArg(group);
    this._removeByDomain(group, false, context, callback);
  },

  removeBySubdomain: function CPS2_removeBySubdomain(group, context, callback) {
    checkGroupArg(group);
    this._removeByDomain(group, true, context, callback);
  },

  removeAllGlobals: function CPS2_removeAllGlobals(context, callback) {
    this._removeByDomain(null, false, context, callback);
  },

  _removeByDomain: function CPS2__removeByDomain(
    group,
    includeSubdomains,
    context,
    callback
  ) {
    group = this._parseGroup(group);
    checkCallbackArg(callback, false);

    // Invalidate the cached values so consumers accessing the cache between now
    // and when the operation finishes don't get old data.
    for (let sgroup of this._cache.matchGroups(group, includeSubdomains)) {
      this._cache.removeGroup(sgroup);
    }

    let isPrivate = context?.usePrivateBrowsing;
    let stmts = [];

    // For null we clear normal and private browsing data. false only clears
    // normal browsing data. true only clears private browsing data.
    if (context == null || !isPrivate) {
      // First get the matching prefs, then delete groups and prefs that reference
      // deleted groups.
      if (group) {
        stmts.push(
          this._stmtWithGroupClause(
            group,
            includeSubdomains,
            `
              SELECT groups.name AS grp, settings.name AS name
              FROM prefs
              JOIN settings ON settings.id = prefs.settingID
              JOIN groups ON groups.id = prefs.groupID
              WHERE prefs.groupID IN (${GROUP_CLAUSE})
            `
          )
        );
        stmts.push(
          this._stmtWithGroupClause(
            group,
            includeSubdomains,
            `DELETE FROM groups WHERE id IN (${GROUP_CLAUSE})`
          )
        );
        stmts.push(
          this._stmt(`
          DELETE FROM prefs
          WHERE groupID NOTNULL AND groupID NOT IN (SELECT id FROM groups)
        `)
        );
      } else {
        stmts.push(
          this._stmt(`
          SELECT NULL AS grp, settings.name AS name
          FROM prefs
          JOIN settings ON settings.id = prefs.settingID
          WHERE prefs.groupID IS NULL
        `)
        );
        stmts.push(this._stmt("DELETE FROM prefs WHERE groupID IS NULL"));
      }

      // Finally delete settings that are no longer referenced.
      stmts.push(
        this._stmt(`
        DELETE FROM settings
        WHERE id NOT IN (SELECT DISTINCT settingID FROM prefs)
      `)
      );
    }

    let prefsNonPrivateBrowsing = new ContentPrefStore();

    // Only execute if we have statements to run. stmts is empty for clearing
    // only private browsing data.
    let queryPromise = Promise.resolve([
      Ci.nsIContentPrefCallback2.COMPLETE_OK,
      false,
    ]);
    if (stmts.length) {
      queryPromise = new Promise(resolve => {
        this._execStmts(stmts, {
          onRow: row => {
            let grp = row.getResultByName("grp");
            let name = row.getResultByName("name");
            prefsNonPrivateBrowsing.set(grp, name, undefined);
            this._cache.set(grp, name, undefined);
          },
          onDone: (reason, ok) => {
            resolve([reason, ok]);
          },
          onError: nsresult => {
            cbHandleError(callback, nsresult);
          },
        });
      });
    }

    let prefsPrivateBrowsing = new ContentPrefStore();

    if (isPrivate || context == null) {
      for (let [sgroup, sname] of this._pbStore) {
        if (
          !group ||
          (!includeSubdomains && group == sgroup) ||
          (includeSubdomains &&
            sgroup &&
            this._pbStore.groupsMatchIncludingSubdomains(group, sgroup))
        ) {
          prefsPrivateBrowsing.set(sgroup, sname, undefined);
          this._pbStore.remove(sgroup, sname);
        }
      }
    }

    queryPromise
      .then(([reason, hasNonPrivateEntries]) => {
        // Notify caller of completion.
        cbHandleCompletion(callback, reason);

        // Notify for non PBM changes.
        if (hasNonPrivateEntries) {
          for (let [sgroup, sname] of prefsNonPrivateBrowsing) {
            this._notifyPrefRemoved(sgroup, sname, false);
          }
        }

        // Notify for PBM changes.
        for (let [sgroup, sname] of prefsPrivateBrowsing) {
          this._notifyPrefRemoved(sgroup, sname, true);
        }
      })
      .catch(nsresult => {
        cbHandleError(callback, nsresult);
      });
  },

  _removeAllDomainsSince: function CPS2__removeAllDomainsSince(
    since,
    context,
    callback
  ) {
    checkCallbackArg(callback, false);

    since /= 1000;

    // Invalidate the cached values so consumers accessing the cache between now
    // and when the operation finishes don't get old data.
    // Invalidate all the group cache because we don't know which groups will be removed.
    this._cache.removeAllGroups();

    let isPrivate = context?.usePrivateBrowsing;
    let stmts = [];

    // For null we clear normal and private browsing data. false only clears
    // normal browsing data. true only clears private browsing data.
    if (context == null || !isPrivate) {
      // Get prefs that are about to be removed to notify about their removal.
      let stmt = this._stmt(`
        SELECT groups.name AS grp, settings.name AS name
        FROM prefs
        JOIN settings ON settings.id = prefs.settingID
        JOIN groups ON groups.id = prefs.groupID
        WHERE timestamp >= :since
      `);
      stmt.params.since = since;
      stmts.push(stmt);

      // Do the actual remove.
      stmt = this._stmt(`
        DELETE FROM prefs WHERE groupID NOTNULL AND timestamp >= :since
      `);
      stmt.params.since = since;
      stmts.push(stmt);

      // Cleanup no longer used values.
      stmts = stmts.concat(this._settingsAndGroupsCleanupStmts());
    }

    let prefsNonPrivateBrowsing = new ContentPrefStore();

    // Only execute if we have statements to run. stmts is empty for clearing
    // only private browsing data.
    let queryPromise = Promise.resolve([
      Ci.nsIContentPrefCallback2.COMPLETE_OK,
      false,
    ]);

    if (stmts.length) {
      queryPromise = new Promise(resolve => {
        this._execStmts(stmts, {
          onRow: row => {
            let grp = row.getResultByName("grp");
            let name = row.getResultByName("name");
            prefsNonPrivateBrowsing.set(grp, name, undefined);
            this._cache.set(grp, name, undefined);
          },
          onDone: (reason, ok) => {
            resolve([reason, ok]);
          },
          onError: nsresult => {
            cbHandleError(callback, nsresult);
          },
        });
      });
    }

    let prefsPrivateBrowsing = new ContentPrefStore();

    // This nukes all the groups in _pbStore since we don't have their timestamp
    // information.
    if (isPrivate || context == null) {
      for (let [sgroup, sname] of this._pbStore) {
        if (sgroup) {
          prefsPrivateBrowsing.set(sgroup, sname, undefined);
        }
      }
      this._pbStore.removeAllGroups();
    }

    queryPromise
      .then(([reason, hasNonPrivateEntries]) => {
        // Notify caller of completion.
        cbHandleCompletion(callback, reason);

        // Notify for non PBM changes.
        if (hasNonPrivateEntries) {
          for (let [sgroup, sname] of prefsNonPrivateBrowsing) {
            this._notifyPrefRemoved(sgroup, sname, false);
          }
        }

        // Notify for PBM changes.
        for (let [sgroup, sname] of prefsPrivateBrowsing) {
          this._notifyPrefRemoved(sgroup, sname, true);
        }
      })
      .catch(nsresult => {
        cbHandleError(callback, nsresult);
      });
  },

  removeAllDomainsSince: function CPS2_removeAllDomainsSince(
    since,
    context,
    callback
  ) {
    this._removeAllDomainsSince(since, context, callback);
  },

  removeAllDomains: function CPS2_removeAllDomains(context, callback) {
    this._removeAllDomainsSince(0, context, callback);
  },

  removeByName: function CPS2_removeByName(name, context, callback) {
    checkNameArg(name);
    checkCallbackArg(callback, false);

    // Invalidate the cached values so consumers accessing the cache between now
    // and when the operation finishes don't get old data.
    for (let [group, sname] of this._cache) {
      if (sname == name) {
        this._cache.remove(group, name);
      }
    }

    let isPrivate = context?.usePrivateBrowsing;
    let stmts = [];

    // For null we clear normal and private browsing data. false only clears
    // normal browsing data. true only clears private browsing data.
    if (context == null || !isPrivate) {
      // First get the matching prefs.  Include null if any of those prefs are
      // global.
      let stmt = this._stmt(`
        SELECT groups.name AS grp
        FROM prefs
        JOIN settings ON settings.id = prefs.settingID
        JOIN groups ON groups.id = prefs.groupID
        WHERE settings.name = :name
        UNION
        SELECT NULL AS grp
        WHERE EXISTS (
          SELECT prefs.id
          FROM prefs
          JOIN settings ON settings.id = prefs.settingID
          WHERE settings.name = :name AND prefs.groupID IS NULL
        )
      `);
      stmt.params.name = name;
      stmts.push(stmt);

      // Delete the target settings.
      stmt = this._stmt("DELETE FROM settings WHERE name = :name");
      stmt.params.name = name;
      stmts.push(stmt);

      // Delete prefs and groups that are no longer used.
      stmts.push(
        this._stmt(
          "DELETE FROM prefs WHERE settingID NOT IN (SELECT id FROM settings)"
        )
      );
      stmts.push(
        this._stmt(`
        DELETE FROM groups WHERE id NOT IN (
          SELECT DISTINCT groupID FROM prefs WHERE groupID NOTNULL
        )
      `)
      );
    }

    let prefsNonPrivateBrowsing = new ContentPrefStore();

    // Only execute if we have statements to run. stmts is empty for clearing
    // only private browsing data.
    let queryPromise = Promise.resolve([
      Ci.nsIContentPrefCallback2.COMPLETE_OK,
      false,
    ]);

    if (stmts.length) {
      queryPromise = new Promise(resolve => {
        this._execStmts(stmts, {
          onRow: row => {
            let grp = row.getResultByName("grp");
            prefsNonPrivateBrowsing.set(grp, name, undefined);
            this._cache.set(grp, name, undefined);
          },
          onDone: (reason, ok) => {
            resolve([reason, ok]);
          },
          onError: nsresult => {
            cbHandleError(callback, nsresult);
          },
        });
      });
    }

    let prefsPrivateBrowsing = new ContentPrefStore();

    if (isPrivate || context == null) {
      for (let [sgroup, sname] of this._pbStore) {
        if (sname === name) {
          prefsPrivateBrowsing.set(sgroup, name, undefined);
          this._pbStore.remove(sgroup, name);
        }
      }
    }

    queryPromise
      .then(([reason, hasNonPrivateEntries]) => {
        // Notify caller of completion.
        cbHandleCompletion(callback, reason);

        // Notify for non PBM changes.
        if (hasNonPrivateEntries) {
          for (let [sgroup, ,] of prefsNonPrivateBrowsing) {
            this._notifyPrefRemoved(sgroup, name, false);
          }
        }

        // Notify for PBM changes.
        for (let [sgroup, ,] of prefsPrivateBrowsing) {
          this._notifyPrefRemoved(sgroup, name, true);
        }
      })
      .catch(nsresult => {
        cbHandleError(callback, nsresult);
      });
  },

  /**
   * Returns the cached mozIStorageAsyncStatement for the given SQL.  If no such
   * statement is cached, one is created and cached.
   *
   * @param sql  The SQL query string.
   * @return     The cached, possibly new, statement.
   */
  _stmt: function CPS2__stmt(sql, cachable = true) {
    return {
      sql,
      cachable,
      params: {},
    };
  },

  /**
   * Executes some async statements.
   *
   * @param stmts      An array of mozIStorageAsyncStatements.
   * @param callbacks  An object with the following methods:
   *                   onRow(row) (optional)
   *                     Called once for each result row.
   *                     row: A mozIStorageRow.
   *                   onDone(reason, reasonOK, didGetRow) (required)
   *                     Called when done.
   *                     reason: A nsIContentPrefService2.COMPLETE_* value.
   *                     reasonOK: reason == nsIContentPrefService2.COMPLETE_OK.
   *                     didGetRow: True if onRow was ever called.
   *                   onError(nsresult) (optional)
   *                     Called on error.
   *                     nsresult: The error code.
   */
  _execStmts: async function CPS2__execStmts(stmts, callbacks) {
    let rows;
    let ok = true;
    try {
      // Awaiting the connection has to happen inside the try: callers such as
      // removeByName() do not consume the returned promise, so a rejection here
      // (e.g. the connection was closed while opening) would otherwise surface
      // as an unhandled rejection instead of reaching onError/onDone.
      let conn = await this.conn;
      rows = await executeStatementsInTransaction(conn, stmts);
    } catch (e) {
      ok = false;
      if (callbacks.onError) {
        try {
          callbacks.onError(e);
        } catch (e) {
          console.error(e);
        }
      } else {
        console.error(e);
      }
    }

    if (rows && callbacks.onRow) {
      for (let row of rows) {
        try {
          callbacks.onRow(row);
        } catch (e) {
          console.error(e);
        }
      }
    }

    try {
      callbacks.onDone(
        ok
          ? Ci.nsIContentPrefCallback2.COMPLETE_OK
          : Ci.nsIContentPrefCallback2.COMPLETE_ERROR,
        ok,
        rows && !!rows.length
      );
    } catch (e) {
      console.error(e);
    }
  },

  _groupForDataURI(groupStr) {
    // Do not process a massive string.
    groupStr = groupStr.substring(0, 256);
    // Leave only the mimetype, and use text/plain if no mimetype is provided.
    return groupStr.match(/^data:[^;,]*/i)[0].replace(/:$/, ":text/plain");
  },

  /**
   * Parses the domain (the "group", to use the database's term) from the given
   * string.
   *
   * @param groupStr  Assumed to be either a string or falsey.
   * @return          If groupStr is a valid URL string, returns the domain of
   *                  that URL.  If groupStr is some other nonempty string,
   *                  returns groupStr itself.  Otherwise returns null.
   *                  The return value is truncated at GROUP_NAME_MAX_LENGTH.
   */
  _parseGroup: function CPS2__parseGroup(groupStr) {
    if (!groupStr) {
      return null;
    }
    // Check for 'data:' without URI parsing because parsing large data URIs
    // is expensive.
    if (groupStr.startsWith("data:")) {
      return this._groupForDataURI(groupStr);
    }
    try {
      var groupURI = Services.io.newURI(groupStr);
      // Check for 'data' again because URLs with leading whitespace or
      // similar will have skipped the previous check.
      if (groupURI.schemeIs("data")) {
        return this._groupForDataURI(groupURI.spec);
      }
      groupStr = HostnameGrouper_group(groupURI);
    } catch (err) {}
    return groupStr.substring(
      0,
      Ci.nsIContentPrefService2.GROUP_NAME_MAX_LENGTH - 1
    );
  },

  _schedule: function CPS2__schedule(fn) {
    Services.tm.dispatchToMainThread(fn.bind(this));
  },

  // A hash of arrays of observers, indexed by setting name.
  _observers: new Map(),

  // An array of generic observers, which observe all settings.
  _genericObservers: new Set(),

  addObserverForName(aName, aObserver) {
    let observers;
    if (aName) {
      observers = this._observers.get(aName);
      if (!observers) {
        observers = new Set();
        this._observers.set(aName, observers);
      }
    } else {
      observers = this._genericObservers;
    }

    observers.add(aObserver);
  },

  removeObserverForName(aName, aObserver) {
    let observers;
    if (aName) {
      observers = this._observers.get(aName);
      if (!observers) {
        return;
      }
    } else {
      observers = this._genericObservers;
    }

    observers.delete(aObserver);
  },

  /**
   * Construct a list of observers to notify about a change to some setting,
   * putting setting-specific observers before before generic ones, so observers
   * that initialize individual settings (like the page style controller)
   * execute before observers that display multiple settings and depend on them
   * being initialized first (like the content prefs sidebar).
   */
  _getObservers(aName) {
    let genericObserverList = Array.from(this._genericObservers);
    if (aName) {
      let observersForName = this._observers.get(aName);
      if (observersForName) {
        return Array.from(observersForName).concat(genericObserverList);
      }
    }
    return genericObserverList;
  },

  /**
   * Notify all observers about the removal of a preference.
   */
  _notifyPrefRemoved: function ContentPrefService__notifyPrefRemoved(
    aGroup,
    aName,
    aIsPrivate
  ) {
    for (var observer of this._getObservers(aName)) {
      try {
        observer.onContentPrefRemoved(aGroup, aName, aIsPrivate);
      } catch (ex) {
        console.error(ex);
      }
    }
  },

  /**
   * Notify all observers about a preference change.
   */
  _notifyPrefSet: function ContentPrefService__notifyPrefSet(
    aGroup,
    aName,
    aValue,
    aIsPrivate
  ) {
    for (var observer of this._getObservers(aName)) {
      try {
        observer.onContentPrefSet(aGroup, aName, aValue, aIsPrivate);
      } catch (ex) {
        console.error(ex);
      }
    }
  },

  extractDomain: function CPS2_extractDomain(str) {
    return this._parseGroup(str);
  },

  /**
   * Tests use this as a backchannel by calling it directly.
   *
   * @param subj   This value depends on topic.
   * @param topic  The backchannel "method" name.
   */
  observe: function CPS2_observe(subj, topic) {
    switch (topic) {
      case "profile-before-change":
        this._destroy();
        break;
      case "last-pb-context-exited":
        this._pbStore.removeAll();
        break;
      case "test:reset": {
        let fn = subj.QueryInterface(Ci.xpcIJSWeakReference).get();
        this._reset(fn);
        break;
      }
      case "test:db": {
        let obj = subj.QueryInterface(Ci.xpcIJSWeakReference).get();
        obj.value = this.conn;
        break;
      }
    }
  },

  /**
   * Removes all state from the service.  Used by tests.
   *
   * @param callback  A function that will be called when done.
   */
  async _reset(callback) {
    this._pbStore.removeAll();
    this._cache.removeAll();

    this._observers = new Map();
    this._genericObservers = new Set();

    let tables = ["prefs", "groups", "settings"];
    let stmts = tables.map(t => this._stmt(`DELETE FROM ${t}`));
    this._execStmts(stmts, {
      onDone: () => {
        callback();
      },
    });
  },

  QueryInterface: ChromeUtils.generateQI([
    "nsIContentPrefService2",
    "nsIObserver",
  ]),

  // Database Creation & Access

  _dbVersion: 6,

  _dbSchema: {
    tables: {
      groups:
        "id           INTEGER PRIMARY KEY, \
                   name         TEXT NOT NULL",

      settings:
        "id           INTEGER PRIMARY KEY, \
                   name         TEXT NOT NULL",

      prefs:
        "id           INTEGER PRIMARY KEY, \
                   groupID      INTEGER REFERENCES groups(id), \
                   settingID    INTEGER NOT NULL REFERENCES settings(id), \
                   value        BLOB, \
                   timestamp    INTEGER NOT NULL DEFAULT 0", // Storage in seconds, API in ms. 0 for migrated values.
    },
    indices: {
      groups_idx: {
        table: "groups",
        columns: ["name"],
      },
      settings_idx: {
        table: "settings",
        columns: ["name"],
      },
      prefs_idx: {
        table: "prefs",
        columns: ["timestamp", "groupID", "settingID"],
      },
    },
  },

  _debugLog: false,

  log: function CPS2_log(aMessage) {
    if (this._debugLog) {
      Services.console.logStringMessage("ContentPrefService2: " + aMessage);
    }
  },

  async _getConnection(aAttemptNum = 0) {
    if (
      Services.startup.isInOrBeyondShutdownPhase(
        Ci.nsIAppStartup.SHUTDOWN_PHASE_APPSHUTDOWN
      )
    ) {
      throw new Error("Can't open content prefs, we're in shutdown.");
    }
    let path = PathUtils.join(PathUtils.profileDir, "content-prefs.sqlite");
    let conn;
    let resetAndRetry = async e => {
      if (e.result != Cr.NS_ERROR_FILE_CORRUPTED) {
        throw e;
      }

      if (aAttemptNum >= this.MAX_ATTEMPTS) {
        if (conn) {
          await conn.close();
        }
        this.log("Establishing connection failed too many times. Giving up.");
        throw e;
      }

      try {
        await this._failover(conn, path);
      } catch (e) {
        console.error(e);
        throw e;
      }
      return this._getConnection(++aAttemptNum);
    };
    try {
      conn = await lazy.Sqlite.openConnection({
        path,
        incrementalVacuum: true,
        vacuumOnIdle: true,
      });
    } catch (e) {
      console.error(e);
      return resetAndRetry(e);
    }

    try {
      await this._dbMaybeInit(conn);
    } catch (e) {
      console.error(e);
      return resetAndRetry(e);
    }

    // WAL with synchronous = NORMAL syncs only at checkpoints, avoiding disk
    // churn on rapid pref changes (e.g. per-site zoom) without the corruption
    // risk of synchronous = OFF.
    await conn.execute("PRAGMA journal_mode = WAL");
    await conn.execute("PRAGMA wal_autocheckpoint = 16");
    await conn.execute("PRAGMA synchronous = NORMAL");

    return conn;
  },

  async _failover(aConn, aPath) {
    this.log("Cleaning up DB file - close & remove & backup.");
    if (aConn) {
      await aConn.close();
    }
    let uniquePath = await IOUtils.createUniqueFile(
      PathUtils.parent(aPath),
      PathUtils.filename(aPath) + ".corrupt",
      0o600
    );
    await IOUtils.copy(aPath, uniquePath);
    await IOUtils.remove(aPath);
    this.log("Completed DB cleanup.");
  },

  _dbMaybeInit: async function CPS2__dbMaybeInit(aConn) {
    let version = parseInt(await aConn.getSchemaVersion(), 10);
    this.log("Schema version: " + version);

    if (version == 0) {
      await this._dbCreateSchema(aConn);
    } else if (version != this._dbVersion) {
      await this._dbMigrate(aConn, version, this._dbVersion);
    }
  },

  _createTable: async function CPS2__createTable(aConn, aName) {
    let tSQL = this._dbSchema.tables[aName];
    this.log("Creating table " + aName + " with " + tSQL);
    await aConn.execute(`CREATE TABLE ${aName} (${tSQL})`);
  },

  _createIndex: async function CPS2__createTable(aConn, aName) {
    let index = this._dbSchema.indices[aName];
    let statement =
      "CREATE INDEX IF NOT EXISTS " +
      aName +
      " ON " +
      index.table +
      "(" +
      index.columns.join(", ") +
      ")";
    await aConn.execute(statement);
  },

  _dbCreateSchema: async function CPS2__dbCreateSchema(aConn) {
    await aConn.executeTransaction(async () => {
      this.log("Creating DB -- tables");
      for (let name in this._dbSchema.tables) {
        await this._createTable(aConn, name);
      }

      this.log("Creating DB -- indices");
      for (let name in this._dbSchema.indices) {
        await this._createIndex(aConn, name);
      }

      await aConn.setSchemaVersion(this._dbVersion);
    });
  },

  _dbMigrate: async function CPS2__dbMigrate(aConn, aOldVersion, aNewVersion) {
    /**
     * Migrations should follow the template rules in bug 1074817 comment 3 which are:
     * 1. Migration should be incremental and non-breaking.
     * 2. It should be idempotent because one can downgrade an upgrade again.
     * On downgrade:
     * 1. Decrement schema version so that upgrade runs the migrations again.
     */
    await aConn.executeTransaction(async () => {
      for (let i = aOldVersion; i < aNewVersion; i++) {
        let migrationName = "_dbMigrate" + i + "To" + (i + 1);
        if (typeof this[migrationName] != "function") {
          throw new Error(
            "no migrator function from version " +
              aOldVersion +
              " to version " +
              aNewVersion
          );
        }
        await this[migrationName](aConn);
      }
      await aConn.setSchemaVersion(aNewVersion);
    });
  },

  _dbMigrate1To2: async function CPS2___dbMigrate1To2(aConn) {
    await aConn.execute("ALTER TABLE groups RENAME TO groupsOld");
    await this._createTable(aConn, "groups");
    await aConn.execute(`
      INSERT INTO groups (id, name)
      SELECT id, name FROM groupsOld
    `);

    await aConn.execute("DROP TABLE groupers");
    await aConn.execute("DROP TABLE groupsOld");
  },

  _dbMigrate2To3: async function CPS2__dbMigrate2To3(aConn) {
    for (let name in this._dbSchema.indices) {
      await this._createIndex(aConn, name);
    }
  },

  _dbMigrate3To4: async function CPS2__dbMigrate3To4(aConn) {
    // Add timestamp column if it does not exist yet. This operation is idempotent.
    try {
      await aConn.execute("SELECT timestamp FROM prefs");
    } catch (e) {
      await aConn.execute(
        "ALTER TABLE prefs ADD COLUMN timestamp INTEGER NOT NULL DEFAULT 0"
      );
    }

    // To modify prefs_idx drop it and create again.
    await aConn.execute("DROP INDEX IF EXISTS prefs_idx");
    for (let name in this._dbSchema.indices) {
      await this._createIndex(aConn, name);
    }
  },

  async _dbMigrate4To5(conn) {
    // This is a data migration for browser.download.lastDir. While it may not
    // affect all consumers, it's simpler and safer to do it here than elsewhere.
    await conn.execute(`
      DELETE FROM prefs
      WHERE id IN (
        SELECT p.id FROM prefs p
        JOIN groups g ON g.id = p.groupID
        JOIN settings s ON s.id = p.settingID
        WHERE s.name = 'browser.download.lastDir'
          AND (
          (g.name BETWEEN 'data:' AND 'data:' || X'FFFF') OR
          (g.name BETWEEN 'file:' AND 'file:' || X'FFFF')
        )
      )
    `);
    await conn.execute(`
      DELETE FROM groups WHERE NOT EXISTS (
        SELECT 1 FROM prefs WHERE groupId = groups.id
      )
    `);
    // Trim group names longer than MAX_GROUP_LENGTH.
    await conn.execute(
      `
      UPDATE groups
      SET name = substr(name, 0, :maxlen)
      WHERE LENGTH(name) > :maxlen
      `,
      {
        maxlen: Ci.nsIContentPrefService2.GROUP_NAME_MAX_LENGTH,
      }
    );
  },

  async _dbMigrate5To6(conn) {
    // This is a data migration for blob: URIs, as we started storing their
    // origin when possible.
    // Rather than trying to migrate blob URIs to their origins, that would
    // require multiple steps for a tiny benefit (they never worked anyway),
    // just remove them, as they are one-time generated URLs unlikely to be
    // useful in the future. New inserted blobs will do the right thing.
    await conn.execute(`
      DELETE FROM prefs
      WHERE id IN (
        SELECT p.id FROM prefs p
        JOIN groups g ON g.id = p.groupID
        AND g.name BETWEEN 'blob:' AND 'blob:' || X'FFFF'
      )
    `);
    await conn.execute(`
      DELETE FROM groups WHERE NOT EXISTS (
        SELECT 1 FROM prefs WHERE groupId = groups.id
      )
    `);
  },
};

function checkGroupArg(group) {
  if (!group || typeof group != "string") {
    throw invalidArg("domain must be nonempty string.");
  }
}

function checkNameArg(name) {
  if (!name || typeof name != "string") {
    throw invalidArg("name must be nonempty string.");
  }
}

function checkValueArg(value) {
  if (value === undefined) {
    throw invalidArg("value must not be undefined.");
  }
}

function checkCallbackArg(callback, required) {
  if (callback && !(callback instanceof Ci.nsIContentPrefCallback2)) {
    throw invalidArg("callback must be an nsIContentPrefCallback2.");
  }
  if (!callback && required) {
    throw invalidArg("callback must be given.");
  }
}

function invalidArg(msg) {
  return Components.Exception(msg, Cr.NS_ERROR_INVALID_ARG);
}

// XPCOM Plumbing
