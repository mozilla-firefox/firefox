/**
 * @file Generates the use-design-tokens rule's "Checked properties" docs page
 * from config.mjs, and writes it when run as a script.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

// This is a non-production script.
/* eslint-disable no-console */

import fs from "node:fs";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";
import { propertyConfig, propertyTypes } from "../config.mjs";
import { SYSTEM_COLORS } from "../helpers.mjs";
import { PropertyValidator } from "../property-validator.mjs";

const topsrcdir = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  "../../../../.."
);

export const docsPath = path.join(
  topsrcdir,
  "docs/code-quality/lint/linters/stylelint-plugin-mozilla/rules/use-design-tokens/checked-properties.md"
);

const KNOWN_KEYS = new Set([
  "allow",
  "allowAlias",
  "allowAliasFunctions",
  "allowFunctions",
  "allowUnits",
  "allowedTokens",
  "allowedUnits",
  "aliasTokenTypes",
  "customFixes",
  "customSuggestions",
  "tokenTypes",
  "warnSystemColors",
]);

const code = values => values.map(value => `\`${value}\``).join(", ");

const list = (values, conjunction) => {
  const quoted = values.map(value => `\`${value}\``);
  return quoted.length < 2
    ? quoted.join("")
    : `${quoted.slice(0, -1).join(", ")} ${conjunction} ${quoted.at(-1)}`;
};

const sentenceCase = typeName =>
  typeName
    .replace(/(?<=[a-z])(?=[A-Z])/g, " ")
    .replace(/ [A-Z]/g, s => s.toLowerCase());

const anchor = typeName =>
  `use-design-tokens-${sentenceCase(typeName).toLowerCase().replaceAll(" ", "-")}`;

const isSystemColorList = values =>
  values.length === SYSTEM_COLORS.length &&
  SYSTEM_COLORS.every(color => values.includes(color));

const allowedWords = types =>
  new Set(
    types
      .flatMap(type => type.allow ?? [])
      .concat(...PropertyValidator.GLOBAL_WORDS)
      .map(word => word.toLowerCase())
  );

// PropertyValidator.getFixedValue never rewrites an allowed word.
const listedFixes = type => {
  const allowed = allowedWords([type]);
  return Object.entries(type.customFixes ?? {}).filter(
    ([value]) => !allowed.has(value.toLowerCase())
  );
};

function typeSection(typeName, type) {
  const unknown = Object.keys(type).filter(key => !KNOWN_KEYS.has(key));
  if (unknown.length) {
    throw new Error(
      `${typeName} has keys update-docs.mjs doesn't document: ${unknown.join(", ")}`
    );
  }

  const properties = Object.entries(propertyConfig)
    .filter(([, config]) => config.validTypes.includes(type))
    .map(([property]) => property);
  if (!properties.length) {
    return null;
  }

  const lines = [
    `(${anchor(typeName)})=`,
    "",
    `## ${sentenceCase(typeName)}`,
    "",
  ];
  lines.push(`- **Properties:** ${code(properties)}`);
  if (type.tokenTypes?.length) {
    lines.push(`- **Token categories:** ${code(type.tokenTypes)}`);
  }
  if (type.allowedTokens?.length) {
    lines.push(`- **Other tokens:** ${code(type.allowedTokens)}`);
  }
  if (type.allow?.length) {
    lines.push(`- **Non-token values:** ${code(type.allow)}`);
  }
  if (type.allowFunctions?.length) {
    lines.push(
      `- **Functions:** ${code(type.allowFunctions.map(f => `${f}()`))}`
    );
  }
  if (type.allowUnits) {
    const units = type.allowedUnits?.length
      ? code(type.allowedUnits)
      : "any unit";
    lines.push(`- **Units:** ${units}`);
  }

  const throughLocal = [];
  if (type.aliasTokenTypes?.length) {
    throughLocal.push(`tokens in ${code(type.aliasTokenTypes)}`);
  }
  if (type.allowAlias?.length) {
    throughLocal.push(
      isSystemColorList(type.allowAlias)
        ? "system colors"
        : code(type.allowAlias)
    );
  }
  if (type.allowAliasFunctions?.length) {
    throughLocal.push(code(type.allowAliasFunctions.map(f => `${f}()`)));
  }
  if (throughLocal.length) {
    lines.push(
      `- **Only through a local custom property:** ${throughLocal.join("; ")}`
    );
  }
  const warnings = [];
  if (type.warnSystemColors) {
    warnings.push("they are the whole value");
  }
  if (type.customSuggestions) {
    warnings.push(
      "the tokens that `customSuggestions` maps them to give a valid value in their place"
    );
  }
  if (warnings.length) {
    lines.push(
      `- **System colors:** a warning, not an error, when ${warnings.join(" or when ")}`
    );
  }

  const fixes = listedFixes(type);
  if (fixes.length) {
    lines.push(
      "- **Autofix:**",
      "",
      "  | Value | Fixed to |",
      "  | ----- | -------- |",
      ...fixes.map(([value, fix]) => `  | \`${value}\` | \`${fix}\` |`)
    );
  }
  lines.push("");
  return lines.join("\n");
}

export function generateDocs() {
  const header = `% Generated from config.mjs by
% tools/lint/stylelint/stylelint-plugin-mozilla/scripts/update-docs.mjs.
% Don't edit it by hand; regenerate it with
% \`./mach npm run update-docs --prefix tools/lint/stylelint/stylelint-plugin-mozilla\`.

# Checked properties

The \`use-design-tokens\` rule checks every property below. The sections are the
property types in
{searchfox}\`config.mjs <tools/lint/stylelint/stylelint-plugin-mozilla/config.mjs>\`,
and a property listed under several types accepts a value that any of them
accepts. Where two of a property's types fix the same value, the first type in
its \`validTypes\` wins.

Every checked property also accepts ${list([...PropertyValidator.GLOBAL_WORDS], "and")}.
`;

  const sections = Object.entries(propertyTypes)
    .sort(([a], [b]) => (a < b ? -1 : 1))
    .map(([typeName, type]) => typeSection(typeName, type))
    .filter(Boolean);

  const typed = new Set(Object.values(propertyTypes));
  for (const [property, config] of Object.entries(propertyConfig)) {
    for (const type of config.validTypes) {
      if (!typed.has(type)) {
        throw new Error(
          `${property} uses a type that isn't in propertyTypes in config.mjs`
        );
      }
    }
    // PropertyValidator unions allowedUnits, so one type's list would also
    // restrict a type that allows any unit, and the page would be wrong.
    const unitTypes = config.validTypes.filter(type => type.allowUnits);
    if (
      unitTypes.some(type => type.allowedUnits?.length) &&
      unitTypes.some(type => !type.allowedUnits?.length)
    ) {
      throw new Error(
        `${property} mixes a type that allows any unit with one that restricts units`
      );
    }
    const allowed = allowedWords(config.validTypes);
    for (const type of config.validTypes) {
      for (const [value] of listedFixes(type)) {
        if (allowed.has(value.toLowerCase())) {
          throw new Error(
            `${property} allows ${value}, so the page would list a fix for it that never applies`
          );
        }
      }
    }
  }

  return [header, ...sections].join("\n");
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  fs.writeFileSync(docsPath, generateDocs());
  console.log(`Wrote ${path.relative(topsrcdir, docsPath)}`);
}
