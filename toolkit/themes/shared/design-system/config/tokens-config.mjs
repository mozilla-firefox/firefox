/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// eslint-disable-next-line import-x/no-unresolved
import StyleDictionary from "style-dictionary";
import { COLLECTIONS } from "./helpers/figma-format/constants.mjs";
import { createDesktopFormat } from "./helpers/desktop-format/create-desktop-format.mjs";
import { createFigmaFormat } from "./helpers/figma-format/create-figma-format.mjs";
import { createNovaNewtabFormat } from "./helpers/nova-newtab-format/create-nova-newtab-format.mjs";
import { createTokensTableFormat } from "./helpers/tokens-table-format/create-tokens-table-format.mjs";
import {
  getExternalComponentFileConfig,
  getExternalComponentFormatConfig,
} from "./helpers/general/get-component-info.mjs";
import { getOverrideFiles } from "./helpers/figma-format/get-override-files.mjs";
import { getOverrideFormats } from "./helpers/figma-format/get-override-formats.mjs";
import { jsonParser } from "./helpers/general/json-parser.mjs";
import { tokenPaths } from "./helpers/general/token-paths.mjs";
import {
  figmaAttributeTransform,
  figmaNameTransform,
} from "./helpers/figma-format/transforms.mjs";

StyleDictionary.registerTransform(figmaNameTransform);

StyleDictionary.registerTransform(figmaAttributeTransform);

export default {
  source: tokenPaths,
  hooks: {
    parsers: {
      "json-parser": {
        pattern: /\.json$/,
        parser: jsonParser,
      },
    },
    formats: {
      "css/variables/shared": createDesktopFormat(),
      "css/variables/brand": createDesktopFormat({ surface: "brand" }),
      "css/variables/platform": createDesktopFormat({ surface: "platform" }),
      "css/variables/nova-newtab": createNovaNewtabFormat(),
      "javascript/tokens-table": args => createTokensTableFormat(args, false),
      "javascript/semantic-categories": args =>
        createTokensTableFormat(args, true),
      "json/figma/colors": createFigmaFormat(COLLECTIONS.colors),
      "json/figma/primitives": createFigmaFormat(COLLECTIONS.primitives),
      "json/figma/theme": createFigmaFormat(COLLECTIONS.theme),
      ...getExternalComponentFormatConfig(),
      ...getOverrideFormats(),
    },
  },
  parsers: ["json-parser"],
  platforms: {
    css: {
      options: {
        outputReferences: true,
        showFileHeader: false,
      },
      transformGroup: "css",
      files: [
        {
          destination: "dist/tokens-shared.css",
          format: "css/variables/shared",
        },
        {
          destination: "dist/tokens-brand.css",
          format: "css/variables/brand",
        },
        {
          destination: "dist/tokens-platform.css",
          format: "css/variables/platform",
        },
        {
          destination:
            "../../../../browser/extensions/newtab/content-src/styles/nova/_tokens.scss",
          format: "css/variables/nova-newtab",
        },
        ...getExternalComponentFileConfig(),
      ],
    },
    tables: {
      options: {
        outputReferences: true,
        showFileHeader: false,
      },
      transformGroup: "css",
      files: [
        {
          destination: "dist/tokens-table.mjs",
          format: "javascript/tokens-table",
        },
        {
          destination: "dist/semantic-categories.mjs",
          format: "javascript/semantic-categories",
        },
      ],
    },
    figma: {
      options: {
        outputReferences: true,
        showFileHeader: false,
      },
      transforms: ["name/figma", "attribute/figma"],
      files: [
        {
          destination: "dist/tokens-figma-colors.json",
          format: "json/figma/colors",
        },
        {
          destination: "dist/tokens-figma-primitives.json",
          format: "json/figma/primitives",
        },
        {
          destination: "dist/tokens-figma-theme.json",
          format: "json/figma/theme",
        },
        ...getOverrideFiles(),
      ],
    },
  },
};
