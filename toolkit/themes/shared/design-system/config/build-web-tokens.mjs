/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-disable no-console */

import { basename } from "node:path";
import { parseArgs } from "node:util";
import process from "node:process";
// eslint-disable-next-line import-x/no-unresolved
import StyleDictionary from "style-dictionary";
import { createWebFormat } from "./helpers/web-format/create-web-format.mjs";
import { getExternalComponentInfo } from "./helpers/general/get-component-info.mjs";
import { jsonParser } from "./helpers/general/json-parser.mjs";
import { OVERRIDE_IDENTIFIERS } from "./helpers/general/override-identifiers.mjs";
import { tokenPaths } from "./helpers/general/token-paths.mjs";

const getWebTokensConfig = ({ overrideIdentifier } = {}) => {
  const components = getExternalComponentInfo().filter(({ isWeb }) => isWeb);
  const componentNames = components.map(({ componentName }) => componentName);

  return {
    source: tokenPaths,
    hooks: {
      parsers: {
        "json-parser": {
          pattern: /\.json$/,
          parser: jsonParser,
        },
      },
      formats: {
        "css/variables/web": createWebFormat({ overrideIdentifier }),
        "css/variables/web/all": createWebFormat({
          overrideIdentifier,
          includeComponents: componentNames,
        }),
        ...Object.fromEntries(
          componentNames.map(componentName => [
            `css/variables/web/${componentName}`,
            createWebFormat({ overrideIdentifier, componentName }),
          ])
        ),
      },
    },
    parsers: ["json-parser"],
    platforms: {
      web: {
        options: {
          outputReferences: true,
          showFileHeader: false,
        },
        transformGroup: "css",
        files: [
          {
            destination: "dist/web/acorn-tokens.css",
            format: "css/variables/web",
          },
          {
            destination: "dist/web/acorn-tokens-all.css",
            format: "css/variables/web/all",
          },
          ...components.map(({ componentName, destination }) => ({
            destination: `dist/web/components/${basename(destination)}`,
            format: `css/variables/web/${componentName}`,
          })),
        ],
      },
    },
  };
};

const { values } = parseArgs({
  options: { override: { type: "string" } },
});

if (
  values.override &&
  !OVERRIDE_IDENTIFIERS.some(({ id }) => id === values.override)
) {
  console.error(
    `Unknown override "${values.override}", expected one of: ${OVERRIDE_IDENTIFIERS.map(({ id }) => id).join(", ")}`
  );
  process.exit(1);
}

const sd = new StyleDictionary(
  getWebTokensConfig({ overrideIdentifier: values.override })
);
await sd.buildAllPlatforms();
