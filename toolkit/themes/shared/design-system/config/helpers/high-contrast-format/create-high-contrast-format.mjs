/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { customFileHeader } from "../general/custom-file-header.mjs";
import { getExternalComponentInfo } from "../general/get-component-info.mjs";
import { OVERRIDE_IDENTIFIERS } from "../general/override-identifiers.mjs";
import { getTokensByLayer } from "../desktop-format/get-tokens-by-layer.mjs";

const SURFACES = [undefined, "brand", "platform"];

/**
 * Lists the tokens that have a value in a `tokens-prefers-contrast` or
 * `tokens-forced-colors` layer of any generated sheet, Nova layers included.
 *
 * @param {object} args
 * @param {object} args.dictionary - The Style Dictionary dictionary.
 * @returns {string}
 */
export const createHighContrastFormat = ({ dictionary }) => {
  const prefersContrast = new Set();
  const forcedColors = new Set();
  const addNames = (set, tokens) => {
    for (const token of tokens) {
      const tokenName = token.match(/^\s*(--[\w-]+):/m)?.[1];
      if (tokenName) {
        set.add(tokenName);
      }
    }
  };

  const componentNames = [
    "",
    ...getExternalComponentInfo().map(({ componentName }) => componentName),
  ];
  const overrideIdentifiers = ["", ...OVERRIDE_IDENTIFIERS.map(({ id }) => id)];
  for (const componentName of componentNames) {
    for (const surface of componentName ? [undefined] : SURFACES) {
      for (const overrideIdentifier of overrideIdentifiers) {
        const layers = getTokensByLayer({
          surface,
          componentName,
          dictionary,
          overrideIdentifier,
        });
        addNames(prefersContrast, layers.prefersContrast);
        addNames(forcedColors, layers.forcedColors);
      }
    }
  }

  const sorted = set => JSON.stringify([...set].sort());
  return `${customFileHeader({ platform: "high-contrast-tokens" })}
  export const prefersContrastTokens = ${sorted(prefersContrast)};

  export const forcedColorsTokens = ${sorted(forcedColors)};`;
};
