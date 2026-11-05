/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    logModule: "NotificationStorage",
    maxLogLevelPref: "dom.webnotifications.loglevel",
  });
});

/** @import {NotificationDB} from "./NotificationDB.sys.mjs" */

export class NotificationStorage {
  /** @type {NotificationDB} */
  db = ChromeUtils.importESModule(
    "moz-src:///dom/notification/NotificationDB.sys.mjs"
  ).db;

  async put(aOrigin, aEntry, aScope) {
    lazy.console.debug(`PUT: ${aOrigin} ${aEntry.id}: ${aEntry.title}`);
    let notification = {
      ...aEntry,

      // Storing QueryInterface confuses XPCOM to think it's passing an XPCOM object
      actions: aEntry.actions.map(rawAction => {
        let actionEntry = { ...rawAction };
        delete actionEntry.QueryInterface;
        return actionEntry;
      }),
      timestamp: new Date().getTime(),
      serviceWorkerRegistrationScope: aScope,
    };
    delete notification.QueryInterface;

    await this.db.queueTask("save", {
      origin: aOrigin,
      notification,
    });
  }

  async get(origin, scope, tag, callback) {
    lazy.console.debug(`GET: ${origin} ${tag}`);
    let notifications = await this.db.queueTask("getall", {
      origin,
      scope,
      tag,
    });
    callback.done(notifications);
  }

  /**
   * @param {string} origin The site origin
   * @param {string} id The notification ID
   */
  async getById(origin, id) {
    lazy.console.debug(`GETBYID: ${origin} ${id}`);

    return await this.db.queueTask("get", { origin, id });
  }

  async delete(origin, id) {
    lazy.console.debug(`DELETE: ${id}`);
    await this.db.queueTask("delete", {
      origin,
      id,
    });
  }

  async deleteAllExcept(ids) {
    lazy.console.debug(`DELETEALLEXCEPT: ${ids}`);
    await this.db.queueTask("deleteAllExcept", { ids });
  }

  QueryInterface = ChromeUtils.generateQI(["nsINotificationStorage"]);
}

export class MemoryNotificationStorage extends NotificationStorage {
  db = new (ChromeUtils.importESModule(
    "moz-src:///dom/notification/MemoryNotificationDB.sys.mjs"
  ).MemoryNotificationDB)();
}
