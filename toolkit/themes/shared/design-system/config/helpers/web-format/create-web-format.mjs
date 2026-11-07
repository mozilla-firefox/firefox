/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { customFileHeader } from "../general/custom-file-header.mjs";
import { formatToken } from "../desktop-format/format-token.mjs";
import { groupAndSortTokens } from "../desktop-format/group-and-sort-tokens.mjs";
import { shouldSkipToken } from "../desktop-format/should-skip-token.mjs";

const LAYER_NAME = "acorn.tokens";

const getVariableName = ({ token }) => token.split(":")[0];

/**
 * Collect the web values for every token, keyed by CSS variable name. Brand
 * values always take precedence and platform values are never used.
 *
 * @param {object} params
 * @param {object} params.dictionary - The style-dictionary dictionary.
 * @param {string} [params.overrideIdentifier] - Only collect the values from this set of override tokens.
 * @param {string} [params.componentName] - Only collect the tokens for this component.
 * @returns {{ foundation: Map, prefersContrast: Map, forcedColors: Map }}
 */
const getWebTokens = ({
  dictionary,
  overrideIdentifier = "",
  componentName = "",
}) => {
  const layers = {
    foundation: new Map(),
    prefersContrast: new Map(),
    forcedColors: new Map(),
  };

  for (const token of dictionary.allTokens) {
    if (shouldSkipToken({ overrideIdentifier, componentName, token })) {
      continue;
    }

    let value = token.original.value;
    if (overrideIdentifier && value[overrideIdentifier]?.value) {
      value = value[overrideIdentifier].value;
    }

    const format = originalValue =>
      formatToken({
        originalName: token.name,
        originalValue,
        tokens: dictionary.tokens,
        comment: token.comment,
        overrideIdentifier,
      });

    const layerTokens = {
      foundation: format(value.brand) ?? format(value),
      prefersContrast: format(
        value.brand?.prefersContrast ?? value.prefersContrast
      ),
      forcedColors: format(value.brand?.forcedColors ?? value.forcedColors),
    };

    for (const [layer, formatted] of Object.entries(layerTokens)) {
      if (formatted) {
        layers[layer].set(getVariableName(formatted), formatted);
      }
    }
  }

  return layers;
};

const mergeLayers = (target, source) => {
  for (const [layer, tokens] of Object.entries(source)) {
    for (const [variableName, token] of tokens) {
      target[layer].set(variableName, token);
    }
  }
  return target;
};

const getLayers = ({ dictionary, overrideIdentifier, componentName }) => {
  const layers = getWebTokens({ dictionary, componentName });
  if (overrideIdentifier) {
    mergeLayers(
      layers,
      getWebTokens({ dictionary, overrideIdentifier, componentName })
    );
  }
  return layers;
};

/**
 * Creates a format that outputs a single CSS file for use on the web, without
 * any of the chrome-only platform or pref checks.
 *
 * @param {object} [options]
 * @param {string} [options.overrideIdentifier] - Set of override tokens (e.g. "nova") to apply on top of the base tokens.
 * @param {string} [options.componentName] - Only output this component's tokens, scoped to :host for use in its shadow DOM.
 * @param {string[]} [options.includeComponents] - Components whose tokens are also output alongside the base tokens.
 */
export const createWebFormat =
  ({ overrideIdentifier, componentName, includeComponents = [] } = {}) =>
  ({ dictionary, file }) => {
    const layers = getLayers({ dictionary, overrideIdentifier, componentName });
    for (const includedComponent of includeComponents) {
      mergeLayers(
        layers,
        getLayers({
          dictionary,
          overrideIdentifier,
          componentName: includedComponent,
        })
      );
    }

    const getRuleCSS = tokens =>
      `${componentName ? ":host" : ":root"} {
  ${groupAndSortTokens({ tokens: [...tokens.values()], componentName }).join("\n  ")}
}`;

    let rules = [getRuleCSS(layers.foundation)];
    if (layers.prefersContrast.size) {
      rules.push(`@media (prefers-contrast) {
${getRuleCSS(layers.prefersContrast)}
}`);
    }
    if (layers.forcedColors.size) {
      rules.push(`@media (forced-colors: active) {
${getRuleCSS(layers.forcedColors)}
}`);
    }

    return `${customFileHeader({ platform: "web", file })}@layer ${LAYER_NAME} {
${rules.join("\n\n")}
}
`;
  };
