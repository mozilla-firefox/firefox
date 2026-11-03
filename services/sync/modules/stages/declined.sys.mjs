/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This file contains code for maintaining the set of declined engines,
 * in conjunction with EngineManager.
 */

import { Log } from "resource://gre/modules/Log.sys.mjs";

import { CommonUtils } from "resource://services-common/utils.sys.mjs";
import { Observers } from "resource://services-common/observers.sys.mjs";

export var DeclinedEngines = function (service) {
  this._log = Log.repository.getLogger("Sync.Declined");
  this._log.manageLevelFromPref("services.sync.log.logger.declined");

  this.service = service;
};

DeclinedEngines.prototype = {
  updateDeclined(meta, engineManager = this.service.engineManager) {
    let enabled = new Set(engineManager.getEnabled().map(e => e.name));
    let known = new Set(engineManager.getAll().map(e => e.name));
    let remoteDeclined = new Set(meta.payload.declined || []);
    let localDeclined = new Set(engineManager.getDeclined());

    this._log.debug(
      "Handling remote declined: " + JSON.stringify([...remoteDeclined])
    );
    this._log.debug(
      "Handling local declined: " + JSON.stringify([...localDeclined])
    );

    // Any engines that are locally enabled should be removed from the remote
    // declined list.
    //
    // Any engines that are locally declined should be added to the remote
    // declined list.
    let newDeclined = CommonUtils.union(
      localDeclined,
      CommonUtils.difference(remoteDeclined, enabled)
    );

    // If our declined set has changed, put it into the meta object and mark
    // it as changed.
    let declinedChanged = !CommonUtils.setEqual(newDeclined, remoteDeclined);
    this._log.debug("Declined changed? " + declinedChanged);
    if (declinedChanged) {
      meta.changed = true;
      meta.payload.declined = [...newDeclined];
    }

    // Update the engine manager regardless.
    engineManager.setDeclined(newDeclined);

    // Any engines that are locally known, locally disabled, and not remotely
    // or locally declined, are candidates for enablement.
    let undecided = CommonUtils.difference(
      CommonUtils.difference(known, enabled),
      newDeclined
    );
    if (undecided.size) {
      let subject = {
        declined: newDeclined,
        enabled,
        known,
        undecided,
      };
      CommonUtils.nextTick(() => {
        Observers.notify("weave:engines:notdeclined", subject);
      });
    }

    return declinedChanged;
  },
};
