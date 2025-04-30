/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* globals AboutCompatBroker, AVAILABLE_SHIMS,
           CUSTOM_FUNCTIONS, Interventions, Shims */

var interventions, shims;

const AVAILABLE_INTERVENTIONS =
#include data/interventions.json

try {
  interventions = new Interventions(AVAILABLE_INTERVENTIONS, CUSTOM_FUNCTIONS);
  interventions.bootup();
} catch (e) {
  console.error("Interventions failed to start", e);
  interventions = undefined;
}

try {
  shims = new Shims(AVAILABLE_SHIMS);
} catch (e) {
  console.error("Shims failed to start", e);
  shims = undefined;
}

try {
  const aboutCompatBroker = new AboutCompatBroker({
    interventions,
    shims,
  });
  aboutCompatBroker.bootup();
} catch (e) {
  console.error("about:compat broker failed to start", e);
}
