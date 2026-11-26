/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

"use strict";

/* global global, __dirname */

const {
  setMocksInGlobal,
} = require("resource://devtools/client/shared/test-helpers/shared-node-helpers.js");
setMocksInGlobal();

const {
  createLocalizationMock,
} = require("resource://devtools/client/shared/test-helpers/jest-fixtures/Localization.js");
global.Localization = createLocalizationMock(require("@fluent/bundle"));

// Configure enzyme with React 16 adapter.
const Enzyme = require("enzyme");
const Adapter = require("enzyme-adapter-react-16");
Enzyme.configure({ adapter: new Adapter() });

global.Components = { stack: { caller: "" }, Constructor: () => {} };

if (!global.ResizeObserver) {
  global.ResizeObserver = class ResizeObserver {
    observe() {}
    unobserve() {}
    disconnect() {}
  };
}

const mcRoot = `${__dirname}/../../../../../`;
const { pref } = require(
  mcRoot + "devtools/client/shared/test-helpers/jest-fixtures/Services"
);
pref("devtools.debugger.remote-timeout", 10000);
pref("devtools.hud.loglimit", 10000);
pref("devtools.webconsole.filter.error", true);
pref("devtools.webconsole.filter.warn", true);
pref("devtools.webconsole.filter.info", true);
pref("devtools.webconsole.filter.log", true);
pref("devtools.webconsole.filter.debug", true);
pref("devtools.webconsole.filter.css", false);
pref("devtools.webconsole.filter.net", false);
pref("devtools.webconsole.filter.netxhr", false);
pref("devtools.webconsole.filter.chrome", true);
pref("devtools.webconsole.filter.content", true);
pref("devtools.webconsole.inputHistoryCount", 300);
pref("devtools.webconsole.persistlog", false);
pref("devtools.webconsole.timestampMessages", false);
pref("devtools.webconsole.sidebarToggle", true);
pref("devtools.webconsole.groupSimilarMessages", true);
pref("devtools.webconsole.input.editor", false);
pref("devtools.webconsole.input.autocomplete", true);
pref("devtools.webconsole.input.eagerEvaluation", true);
pref("devtools.browserconsole.enableNetworkMonitoring", false);
pref("devtools.webconsole.input.editorWidth", 800);
pref("devtools.webconsole.input.editorOnboarding", true);
pref("devtools.discovery.log", false);
