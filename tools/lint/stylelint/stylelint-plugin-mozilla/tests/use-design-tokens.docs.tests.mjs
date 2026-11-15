/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import assert from "node:assert/strict";
import fs from "node:fs";
import { test } from "node:test";
import { docsPath, generateDocs } from "../scripts/update-docs.mjs";

test("use-design-tokens checked-properties.md matches config.mjs", () => {
  assert.equal(
    fs.readFileSync(docsPath, "utf8").replaceAll("\r\n", "\n"),
    generateDocs(),
    "checked-properties.md is out of date: regenerate it with `./mach npm run update-docs --prefix tools/lint/stylelint/stylelint-plugin-mozilla`"
  );
});
