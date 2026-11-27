/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  isMediaCondition,
  isMediaConditionListWithAnd,
  isMediaConditionListWithOr,
  isMediaFeature,
  isMediaFeatureBoolean,
  isMediaFeaturePlain,
  isMediaInParens,
  isMediaNot,
  isMediaQueryWithoutType,
  isMediaQueryWithType,
  parse,
} from "@csstools/media-query-list-parser";
import stylelint from "stylelint";
import valueParser from "postcss-value-parser";
import {
  forcedColorsTokens,
  prefersContrastTokens,
} from "../../../../../toolkit/themes/shared/design-system/dist/high-contrast-tokens.mjs";
import { tokensTable } from "../../../../../toolkit/themes/shared/design-system/dist/semantic-categories.mjs";
import {
  CSS_WIDE_KEYWORDS,
  customPropertiesRead,
  DEPRECATED_SYSTEM_COLORS,
  isDesignToken,
  isSystemColor,
  namespace,
} from "../helpers.mjs";

const {
  utils: { report, ruleMessages, validateOptions },
} = stylelint;

const ruleName = namespace("no-unguarded-high-contrast-token-overrides");

const messages = ruleMessages(ruleName, {
  rejected: (tokenName, conditions, guard) =>
    `"${tokenName}" has ${conditions} values, which this value overrides. ` +
    `Put the declaration under "@media ${guard}".`,
});

const meta = {
  url: "https://firefox-source-docs.mozilla.org/code-quality/lint/linters/stylelint-plugin-mozilla/rules/no-unguarded-high-contrast-token-overrides.html",
  fixable: true,
};

const FORCED_COLORS_TOKENS = new Set(forcedColorsTokens);
const PREFERS_CONTRAST_TOKENS = new Set(prefersContrastTokens);

// The words of a color-mix() that are syntax rather than a value: the
// interpolation method and the percentages.
const COLOR_MIX_SYNTAX =
  /^(?:in|srgb|srgb-linear|display-p3|a98-rgb|prophoto-rgb|rec2020|lab|oklab|xyz|xyz-d50|xyz-d65|hsl|hwb|lch|oklch|shorter|longer|increasing|decreasing|hue|[\d.]+%)$/i;

// The sheets `./mach buildtokens` writes, which define the tokens.
const GENERATED_TOKEN_SHEET =
  /(?:\.tokens\.css|\/design-system\/dist\/.+\.css)$/;

// Base color tokens, e.g. `--color-gray-80` and `--color-white-alpha-20`.
const BASE_COLOR_TOKEN = /^--color-[a-z]+(?:-[a-z]+)*-\d+$/;

/**
 * Whether a parsed value holds a literal anywhere other than a token
 * reference: a color, a number, a keyword such as `transparent` or
 * `currentColor`, a function such as `rgb()` or `linear-gradient()`, or a base
 * color token, at the top level, inside `light-dark()` or `color-mix()`, or in
 * a `var()` fallback. System colors and CSS-wide keywords are not literals.
 *
 * @param {object[]} nodes - postcss-value-parser nodes.
 * @param {boolean} [inColorMix] - Whether the nodes are color-mix() arguments.
 * @returns {boolean}
 */
const hasLiteral = (nodes, inColorMix = false) =>
  nodes.some(node => {
    if (node.type == "word") {
      let word = node.value.toLowerCase();
      return !(
        isSystemColor(word) ||
        DEPRECATED_SYSTEM_COLORS.has(word) ||
        CSS_WIDE_KEYWORDS.has(word) ||
        (inColorMix && COLOR_MIX_SYNTAX.test(word))
      );
    }
    if (node.type != "function") {
      return false;
    }
    let functionName = node.value.toLowerCase();
    if (functionName == "var") {
      if (BASE_COLOR_TOKEN.test(node.nodes[0]?.value ?? "")) {
        return true;
      }
      let comma = node.nodes.findIndex(child => child.type == "div");
      return comma != -1 && hasLiteral(node.nodes.slice(comma + 1));
    }
    if (functionName == "light-dark") {
      return hasLiteral(node.nodes);
    }
    if (functionName == "color-mix") {
      return hasLiteral(node.nodes, true);
    }
    return true;
  });

/**
 * The values each design token has outside its high contrast values, in any
 * theme or surface.
 */
const TOKEN_VALUES = new Map();
const collectValues = (value, values) => {
  if (typeof value == "string") {
    values.push(value);
  } else if (value && typeof value == "object") {
    let keys = Object.keys(value);
    for (let [key, nested] of Object.entries(value)) {
      // `default` repeats a light/dark pair as light-dark().
      let isPair = key == "default" && keys.includes("light");
      if (key != "forcedColors" && key != "prefersContrast" && !isPair) {
        collectValues(nested, values);
      }
    }
  }
};
for (let token of Object.values(tokensTable).flat()) {
  let values = TOKEN_VALUES.get(token.name) ?? [];
  collectValues(token.value, values);
  TOKEN_VALUES.set(token.name, values);
}

const PLAIN_REFERENCE = /^\s*var\(\s*(--[\w-]+)\s*\)\s*$/;

/**
 * Whether a token has a value under a condition: one of its own, or, where
 * every other value it has is a plain `var()` of another token, that token's
 * value there. A computed value such as a color-mix() of a high contrast token
 * is not that token's value.
 *
 * @param {string} tokenName
 * @param {Set<string>} conditionTokens - The tokens with an own value.
 * @param {Set<string>} [visited]
 * @returns {boolean}
 */
const hasConditionValue = (tokenName, conditionTokens, visited = new Set()) => {
  if (conditionTokens.has(tokenName)) {
    return true;
  }
  if (visited.has(tokenName)) {
    return false;
  }
  visited.add(tokenName);
  let values = TOKEN_VALUES.get(tokenName) ?? [];
  let result =
    !!values.length &&
    values.every(value => {
      let reference = value.match(PLAIN_REFERENCE)?.[1];
      return (
        !!reference && hasConditionValue(reference, conditionTokens, visited)
      );
    });
  visited.delete(tokenName);
  return result;
};

/**
 * Whether a value reads a design token that has no value under one of the
 * conditions the overridden token has, fallbacks included. A custom property
 * that is not a design token cannot be judged and is not reported.
 *
 * @param {string} value - The declaration value.
 * @param {{forcedColors: boolean, prefersContrast: boolean}} required
 * @returns {boolean}
 */
const readsWeakerToken = (value, required) =>
  customPropertiesRead(value).some(
    tokenName =>
      (isDesignToken(tokenName) ||
        FORCED_COLORS_TOKENS.has(tokenName) ||
        PREFERS_CONTRAST_TOKENS.has(tokenName)) &&
      ((required.forcedColors &&
        !hasConditionValue(tokenName, FORCED_COLORS_TOKENS)) ||
        (required.prefersContrast &&
          !hasConditionValue(tokenName, PREFERS_CONTRAST_TOKENS)))
  );

/**
 * Evaluates a media feature in Kleene logic: true, false, or null where it
 * depends on something other than the given high-contrast state.
 *
 * @param {string} featureName
 * @param {?string} value - The value of a plain feature, null for a boolean one.
 * @param {{forcedColors: ?boolean, prefersContrast: ?boolean}} state
 * @returns {?boolean}
 */
const evaluateFeature = (featureName, value, state) => {
  featureName = featureName.toLowerCase();
  let active = {
    "forced-colors": state.forcedColors,
    "prefers-contrast": state.prefersContrast,
  }[featureName];
  if (active == null) {
    return null;
  }
  value = value?.toLowerCase();
  if (!value) {
    return active;
  }
  if (value == "none" || value == "no-preference") {
    return !active;
  }
  if (featureName == "forced-colors" && value == "active") {
    return active;
  }
  // `requested`, `more`, `less` and `custom` each need the feature, but the
  // feature does not decide which of them matches.
  return active ? null : false;
};

const not = value => (value == null ? null : !value);
const and = values => {
  if (values.includes(false)) {
    return false;
  }
  return values.every(v => v === true) || null;
};
const or = values => not(and(values.map(not)));

/**
 * Evaluates a media condition node in Kleene logic for a high-contrast state.
 *
 * @param {object} node - A @csstools/media-query-list-parser node.
 * @param {{forcedColors: ?boolean, prefersContrast: ?boolean}} state
 * @returns {?boolean}
 */
const evaluateCondition = (node, state) => {
  if (isMediaCondition(node) || isMediaInParens(node)) {
    return evaluateCondition(node.media, state);
  }
  if (isMediaNot(node)) {
    return not(evaluateCondition(node.media, state));
  }
  if (isMediaConditionListWithAnd(node) || isMediaConditionListWithOr(node)) {
    let values = [node.leading, ...node.list.map(item => item.media)].map(
      child => evaluateCondition(child, state)
    );
    return isMediaConditionListWithAnd(node) ? and(values) : or(values);
  }
  if (isMediaFeature(node)) {
    let { feature } = node;
    if (isMediaFeatureBoolean(feature)) {
      return evaluateFeature(feature.getName(), null, state);
    }
    if (isMediaFeaturePlain(feature)) {
      return evaluateFeature(
        feature.getName(),
        feature.value.toString().trim(),
        state
      );
    }
  }
  return null;
};

// Gecko takes a bare `-moz-pref()` where the media query grammar wants a
// parenthesized condition.
const MOZ_PREF = /-moz-pref\((?:[^()"']|"[^"]*"|'[^']*')*\)/gi;

/**
 * Evaluates a media query list in Kleene logic for a high-contrast state.
 *
 * @param {string} params - The `@media` prelude.
 * @param {{forcedColors: ?boolean, prefersContrast: ?boolean}} state
 * @returns {?boolean}
 */
const evaluateMediaQueryList = (params, state) =>
  or(
    parse(params.replace(MOZ_PREF, "($&)"), {
      preserveInvalidMediaQueries: true,
    }).map(query => {
      if (isMediaQueryWithoutType(query)) {
        return evaluateCondition(query.media, state);
      }
      if (!isMediaQueryWithType(query)) {
        // e.g. Sass interpolation.
        return null;
      }
      // A media type: `all` and `screen` match, anything else is not ours.
      let value =
        ["all", "screen"].includes(query.getMediaType().toLowerCase()) || null;
      if (query.media) {
        value = and([value, evaluateCondition(query.media, state)]);
      }
      return query.getModifier().toLowerCase() == "not" ? not(value) : value;
    })
  );

const LWTHEME_ATTRIBUTE = /\[\s*lwtheme\s*[\]~^|$*=]/;

/**
 * Whether a selector applies only to an add-on theme's `[lwtheme]` root.
 * Forced colors swap in the default theme unless the user turns off
 * browser.theme.forced-colors-override.enabled to keep theirs, and under
 * prefers-contrast the theme's own colors already win over the token's
 * prefers-contrast value.
 *
 * @param {string} selector
 * @returns {boolean}
 */
const requiresLwtheme = selector => {
  let parts = [""];
  for (let i = 0; i < selector.length; i++) {
    let char = selector[i];
    if (char == ",") {
      parts.push("");
    } else if (char == "(") {
      // Skip the parenthesized argument, keeping it only when it is not a
      // :not() argument.
      let depth = 1;
      let j = i + 1;
      for (; j < selector.length && depth; j++) {
        depth += { "(": 1, ")": -1 }[selector[j]] ?? 0;
      }
      if (!/:not$/i.test(parts.at(-1))) {
        parts[parts.length - 1] += selector.slice(i, j);
      }
      i = j - 1;
    } else {
      parts[parts.length - 1] += char;
    }
  }
  return parts.every(part => LWTHEME_ATTRIBUTE.test(part));
};

/**
 * The conditions, of those a token has values for, under which a declaration
 * still overrides it. A condition drops out when the declaration sits in a
 * token layer, under a guard that excludes the condition, or in a block that
 * applies only under the condition, which is deliberate high-contrast styling.
 *
 * @param {object} decl - A PostCSS Declaration.
 * @param {{forcedColors: boolean, prefersContrast: boolean}} required
 * @returns {{forcedColors: boolean, prefersContrast: boolean}}
 */
const overriddenConditions = (decl, required) => {
  let { forcedColors, prefersContrast } = required;
  for (let node = decl.parent; node; node = node.parent) {
    if (node.type != "atrule") {
      continue;
    }
    let atRuleName = node.name.toLowerCase();
    if (atRuleName == "layer" && /^\s*tokens-/i.test(node.params)) {
      return { forcedColors: false, prefersContrast: false };
    }
    if (atRuleName != "media") {
      continue;
    }
    let evaluate = state => evaluateMediaQueryList(node.params, state);
    if (
      evaluate({ forcedColors: true, prefersContrast: true }) === false ||
      evaluate({ forcedColors: false, prefersContrast: null }) === false
    ) {
      forcedColors = false;
    }
    // Forced colors imply prefers-contrast, so without prefers-contrast there
    // are no forced colors either.
    if (
      evaluate({ forcedColors: null, prefersContrast: true }) === false ||
      evaluate({ forcedColors: false, prefersContrast: false }) === false
    ) {
      prefersContrast = false;
    }
  }
  return { forcedColors, prefersContrast };
};

// Forced colors imply prefers-contrast, so the prefers-contrast guard also
// excludes forced colors.
const describe = ({ forcedColors, prefersContrast }) => {
  if (forcedColors && prefersContrast) {
    return {
      conditions: "forced-colors and prefers-contrast",
      guard: "not (prefers-contrast)",
    };
  }
  return forcedColors
    ? { conditions: "forced-colors", guard: "not (forced-colors)" }
    : { conditions: "prefers-contrast", guard: "not (prefers-contrast)" };
};

// New Tab compiles with a Sass that rejects `@media not (…)` and turns
// `@media (not (…))` into `@media (false)`, so its SCSS takes the equivalent
// forms without `not`.
const SASS_SAFE_GUARDS = {
  "not (forced-colors)": "(forced-colors: none)",
  "not (prefers-contrast)": "(prefers-contrast: no-preference)",
};
const NEWTAB_SCSS = /\/browser\/extensions\/newtab\/.+\.scss$/;

/**
 * Moves a declaration into a nested `@media` guard at its own position, or
 * into the guard an earlier fix put right before it.
 *
 * @param {object} decl - A PostCSS Declaration.
 * @param {string} guard - The `@media` prelude.
 */
const wrapInGuard = (decl, guard) => {
  let previous = decl.prev();
  let before = decl.raws.before ?? "\n";
  let multiline = before.includes("\n");
  let indent = before.slice(before.lastIndexOf("\n") + 1);
  let media;
  if (
    previous?.type == "atrule" &&
    previous.name == "media" &&
    previous.params == guard &&
    previous.nodes?.every(node => node.type == "decl")
  ) {
    media = previous;
  } else {
    // Rules that run after this fix report on the new node, so it needs a
    // source position.
    decl.before({
      name: "media",
      params: guard,
      source: decl.source,
      raws: {
        before,
        between: " ",
        afterName: " ",
        after: multiline ? `\n${indent}` : " ",
      },
    });
    media = decl.prev();
  }
  media.raws.semicolon = true;
  decl.remove();
  decl.raws.before = multiline ? `\n${indent}  ` : " ";
  if (decl.value.includes("\n")) {
    if (decl.raws.value) {
      decl.raws.value.raw = decl.raws.value.raw.replaceAll("\n", "\n  ");
    }
    decl.value = decl.value.replaceAll("\n", "\n  ");
    if (decl.raws.value) {
      decl.raws.value.value = decl.value;
    }
  }
  media.append(decl);
};

/**
 * Whether a comment sits at the end of the declaration's line. PostCSS models
 * it as the next sibling, so moving the declaration would strand it.
 *
 * @param {object} decl - A PostCSS Declaration.
 * @returns {boolean}
 */
const hasTrailingComment = decl => {
  let next = decl.next();
  return next?.type == "comment" && !next.raws.before?.includes("\n");
};

/**
 * @param {true} primaryOption
 * @param {{ignoreForcedColors?: boolean}} [secondaryOptions]
 *   ignoreForcedColors: check only the prefers-contrast values, for sheets
 *   that never apply under forced colors.
 */
const ruleFunction = (primaryOption, secondaryOptions) => {
  return (root, result) => {
    const validOptions = validateOptions(
      result,
      ruleName,
      {
        actual: primaryOption,
        possible: [true],
      },
      {
        actual: secondaryOptions,
        possible: { ignoreForcedColors: [true, false] },
        optional: true,
      }
    );

    if (!validOptions) {
      return;
    }

    let file = root.source?.input.file?.replaceAll("\\", "/") ?? "";
    if (GENERATED_TOKEN_SHEET.test(file)) {
      return;
    }

    root.walkDecls(decl => {
      let required = {
        forcedColors:
          !secondaryOptions?.ignoreForcedColors &&
          FORCED_COLORS_TOKENS.has(decl.prop),
        prefersContrast: PREFERS_CONTRAST_TOKENS.has(decl.prop),
      };
      if (!required.forcedColors && !required.prefersContrast) {
        return;
      }
      for (let node = decl.parent; node; node = node.parent) {
        if (node.type == "rule" && requiresLwtheme(node.selector)) {
          return;
        }
      }
      let overridden = overriddenConditions(decl, required);
      if (
        (!overridden.forcedColors && !overridden.prefersContrast) ||
        !(
          hasLiteral(valueParser(decl.value).nodes) ||
          readsWeakerToken(decl.value, overridden)
        )
      ) {
        return;
      }
      let { conditions, guard } = describe(overridden);
      if (NEWTAB_SCSS.test(file)) {
        guard = SASS_SAFE_GUARDS[guard];
      }
      report({
        message: messages.rejected(decl.prop, conditions, guard),
        node: decl,
        result,
        ruleName,
        fix:
          decl.parent.type == "root" || hasTrailingComment(decl)
            ? undefined
            : () => wrapInGuard(decl, guard),
      });
    });
  };
};

ruleFunction.ruleName = ruleName;
ruleFunction.messages = messages;
ruleFunction.meta = meta;

export default ruleFunction;
