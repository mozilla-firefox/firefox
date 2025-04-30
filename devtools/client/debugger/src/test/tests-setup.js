/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

global.Worker = require("workerjs");

import path from "path";
import { prefs } from "../utils/prefs";

import { PrettyPrintDispatcher } from "../workers/pretty-print/index";
import { ParserDispatcher } from "../workers/parser";
import { SearchDispatcher } from "../workers/search/index";

const rootPath = path.join(__dirname, "../../");

jest.setTimeout(20000);

function formatException(reason, p) {
  console && console.log("Unhandled Rejection at:", p, "reason:", reason);
}

export const parserWorker = new ParserDispatcher(
  path.join(rootPath, "src/workers/parser/worker.js")
);
export const prettyPrintWorker = new PrettyPrintDispatcher(
  path.join(rootPath, "src/workers/pretty-print/worker.js")
);
export const searchWorker = new SearchDispatcher(
  path.join(rootPath, "src/workers/search/worker.js")
);

beforeAll(() => {
  process.on("unhandledRejection", formatException);
});

afterAll(() => {
  parserWorker.stop();
  prettyPrintWorker.stop();
  searchWorker.stop();

  process.removeListener("unhandledRejection", formatException);
});

afterEach(() => {});

beforeEach(async () => {
  prefs.expressions = [];
});
