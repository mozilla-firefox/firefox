/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Base tokens are shared across all components and surfaces (e.g. color, typography, spacing).
 * They are not component-specific and always go into the shared CSS output.
 *
 * @type {{ dir: string }}
 */
const BASE_TOKEN_PATH = {
  dir: "src/tokens/base",
};

/**
 * @typedef {object} TokenPath
 * @property {string} dir - Path to the component directory, relative to design-system/.
 * @property {boolean} [isGlobal] - If true, tokens go into the shared CSS output.
 *  If false/absent, each component gets its own CSS output file co-located with its tokens.json.
 * @property {function(string): string} [nameTransform] - Optional transform applied to the filename-derived component name.
 * @property {boolean} [isWeb] - If true, each component also gets its own CSS output file in the web export.
 */
/** @type {TokenPath[]} */
export const COMPONENT_TOKEN_PATHS = [
  {
    dir: "src/tokens/components",
    isGlobal: true,
  },
  {
    dir: "../../../content/widgets",
    nameTransform: componentName => componentName.replace("moz-", ""),
    isWeb: true,
  },
  {
    dir: "../../../../browser/themes/shared",
  },
];

export const tokenPaths = [BASE_TOKEN_PATH, ...COMPONENT_TOKEN_PATHS].map(
  ({ dir }) => `${dir}/**/*.tokens.json`
);
