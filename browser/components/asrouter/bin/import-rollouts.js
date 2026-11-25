/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This is a script to import Nimbus experiments from a given collection into
 * browser/components/asrouter/tests/NimbusRolloutMessageProvider.sys.mjs. By
 * default, it only imports messaging rollouts. This is done so that the content
 * of off-train rollouts can be easily searched. That way, when we are cleaning
 * up old assets (such as Fluent strings), we don't accidentally delete strings
 * that live rollouts are using because it was too difficult to find whether
 * they were in use.
 *
 * This works by fetching the message records from the Nimbus collection and
 * then writing them to the file. The messages are converted from JSON to JS.
 * The file is structured like this:
 * export const NimbusRolloutMessageProvider = {
 *   getMessages() {
 *     return [
 *       { ...message1 },
 *       { ...message2 },
 *     ];
 *   },
 * };
 */

/* eslint-disable no-console */
const chalk = require("chalk");
const https = require("https");
const path = require("path");
const { pathToFileURL } = require("url");
const fs = require("fs");
const util = require("util");
const prettier = require("prettier");
const jsonschema = require("../../../../third_party/js/cfworker/json-schema.js");

const DEFAULT_COLLECTION_ID = "nimbus-desktop-experiments";
const BASE_URL =
  "https://firefox.settings.services.mozilla.com/v1/buckets/main/collections/";
const EXPERIMENTER_URL = "https://experimenter.services.mozilla.com/nimbus/";
const OUTPUT_PATH = "./tests/NimbusRolloutMessageProvider.sys.mjs";
const LICENSE_STRING = `/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */`;

function fetchJSON(url) {
  return new Promise((resolve, reject) => {
    https
      .get(url, resp => {
        let data = "";
        resp.on("data", chunk => {
          data += chunk;
        });
        resp.on("end", () => resolve(JSON.parse(data)));
      })
      .on("error", reject);
  });
}

function isMessageValid(validator, obj) {
  if (validator) {
    const result = validator.validate(obj);
    return result.valid && result.errors.length === 0;
  }
  return true;
}

async function getMessageValidators(skipValidation) {
  if (skipValidation) {
    return { experimentValidator: null, messageValidators: {} };
  }

  async function getSchema(filePath) {
    const file = await util.promisify(fs.readFile)(filePath, "utf8");
    return JSON.parse(file);
  }

  async function getValidator(filePath, { common = false } = {}) {
    const schema = await getSchema(filePath);
    const validator = new jsonschema.Validator(schema);

    if (common) {
      const commonSchema = await getSchema(
        "./content-src/schemas/FxMSCommon.schema.json"
      );
      validator.addSchema(commonSchema);
    }

    return validator;
  }

  const experimentValidator = await getValidator(
    "./content-src/schemas/MessagingExperiment.schema.json"
  );

  const messageValidators = {
    action_only: await getValidator(
      "./content-src/templates/OnboardingMessage/ActionOnlyMessage.schema.json",
      { common: true }
    ),
    bookmarks_bar_button: await getValidator(
      "./content-src/templates/OnboardingMessage/BookmarksBarButton.schema.json",
      { common: true }
    ),
    infobar: await getValidator(
      "./content-src/templates/InfoBar/InfoBar.schema.json",
      { common: true }
    ),
    pb_newtab: await getValidator(
      "./content-src/templates/PBNewtab/NewtabPromoMessage.schema.json",
      { common: true }
    ),
    sidebar_chatbot_promo: await getValidator(
      "./content-src/templates/OnboardingMessage/SidebarChatBotPromo.schema.json",
      { common: true }
    ),
    smart_window_newtab_promo: await getValidator(
      "./content-src/templates/OnboardingMessage/SmartWindowNewtabPromo.schema.json",
      { common: true }
    ),
    spotlight: await getValidator(
      "./content-src/templates/OnboardingMessage/Spotlight.schema.json",
      { common: true }
    ),
    toast_notification: await getValidator(
      "./content-src/templates/ToastNotification/ToastNotification.schema.json",
      { common: true }
    ),
    toolbar_badge: await getValidator(
      "./content-src/templates/OnboardingMessage/ToolbarBadgeMessage.schema.json",
      { common: true }
    ),
    update_action: await getValidator(
      "./content-src/templates/OnboardingMessage/UpdateAction.schema.json",
      { common: true }
    ),
    urlbar_action: await getValidator(
      "./content-src/templates/OnboardingMessage/UrlbarAction.schema.json",
      { common: true }
    ),
    feature_callout: await getValidator(
      // For now, Feature Callout and Spotlight share a common schema
      "./content-src/templates/OnboardingMessage/Spotlight.schema.json",
      { common: true }
    ),
    menu_message: await getValidator(
      "./content-src/templates/OnboardingMessage/MenuMessage.schema.json",
      { common: true }
    ),
    newtab_message: await getValidator(
      "./content-src/templates/OnboardingMessage/NewtabMessage.schema.json",
      { common: true }
    ),
  };

  return { experimentValidator, messageValidators };
}

function annotateMessage({ message, slug, minVersion, maxVersion, url }) {
  const comments = [];
  if (slug) {
    comments.push(`// Nimbus slug: ${slug}`);
  }
  let versionRange = "";
  if (minVersion) {
    versionRange = minVersion;
    if (maxVersion) {
      versionRange += `-${maxVersion}`;
    } else {
      versionRange += "+";
    }
  } else if (maxVersion) {
    versionRange = `0-${maxVersion}`;
  }
  if (versionRange) {
    comments.push(`// Version range: ${versionRange}`);
  }
  if (url) {
    comments.push(`// Recipe: ${url}`);
  }
  return JSON.stringify(message, null, 2).replace(
    /^{/,
    `{ ${comments.join("\n")}`
  );
}

async function format(content) {
  const config = await prettier.resolveConfig("./.prettierrc.js");
  return prettier.format(content, { ...config, filepath: OUTPUT_PATH });
}

async function main() {
  const { default: meow } = await import("meow");
  const { MESSAGING_EXPERIMENTS_DEFAULT_FEATURES } =
    await import("../modules/MessagingExperimentConstants.sys.mjs");

  const fileUrl = pathToFileURL(__filename);

  const cli = meow(
    `
    Usage
      $ node bin/import-rollouts.js [options]

    Options
      -c ID, --collection ID   The Nimbus collection ID to import from
                               default: ${DEFAULT_COLLECTION_ID}
      -e, --experiments        Import all messaging experiments, not just rollouts
      -s, --skip-validation    Skip validation of experiments and messages
      -h, --help               Show this help message

    Examples
      $ node bin/import-rollouts.js --collection nimbus-preview
      $ ./mach npm run import-rollouts --prefix=browser/components/asrouter -- -e
  `,
    {
      description: false,
      // `pkg` is a tiny optimization. It prevents meow from looking for a package
      // that doesn't technically exist. meow searches for a package and changes
      // the process name to the package name. It resolves to the newtab
      // package.json, which would give a confusing name and be wasteful.
      pkg: {
        name: "import-rollouts",
        version: "1.0.0",
      },
      // `importMeta` is required by meow 10+. It was added to support ESM, but
      // meow now requires it, and no longer supports CJS style imports. But it
      // only uses import.meta.url, which can be polyfilled like this:
      importMeta: { url: fileUrl },
      flags: {
        collection: {
          type: "string",
          shortFlag: "c",
          default: DEFAULT_COLLECTION_ID,
        },
        experiments: {
          type: "boolean",
          shortFlag: "e",
          default: false,
        },
        skipValidation: {
          type: "boolean",
          shortFlag: "s",
          default: false,
        },
      },
    }
  );

  const RECORDS_URL = `${BASE_URL}${cli.flags.collection}/records`;

  console.log(`Fetching records from ${chalk.underline.yellow(RECORDS_URL)}`);

  const { data: records } = await fetchJSON(RECORDS_URL);

  if (!Array.isArray(records)) {
    throw new TypeError(
      `Expected records to be an array, got ${typeof records}`
    );
  }

  const recipes = records.filter(
    record =>
      (record.application === "firefox-desktop" ||
        record.appId === "firefox-desktop") &&
      record.featureIds.some(id =>
        MESSAGING_EXPERIMENTS_DEFAULT_FEATURES.includes(id)
      ) &&
      (record.isRollout || cli.flags.experiments)
  );

  const importItems = [];
  const { experimentValidator, messageValidators } = await getMessageValidators(
    cli.flags.skipValidation
  );
  for (const recipe of recipes) {
    const { slug: experimentSlug, branches, targeting } = recipe;
    if (!(experimentSlug && Array.isArray(branches) && branches.length)) {
      continue;
    }
    console.log(
      `Processing ${recipe.isRollout ? "rollout" : "experiment"}: ${chalk.blue(
        experimentSlug
      )}${
        branches.length > 1
          ? ` with ${chalk.underline(`${String(branches.length)} branches`)}`
          : ""
      }`
    );
    const recipeUrl = `${EXPERIMENTER_URL}${experimentSlug}/summary`;
    const [, minVersion] =
      targeting?.match(/\(version\|versionCompare\(\'([0-9]+)\.!\'\) >= 0/) ||
      [];
    const [, maxVersion] =
      targeting?.match(/\(version\|versionCompare\(\'([0-9]+)\.\*\'\) <= 0/) ||
      [];
    let branchIndex = branches.length > 1 ? 1 : 0;
    for (const branch of branches) {
      const { slug: branchSlug, features } = branch;
      console.log(
        `  Processing branch${
          branchIndex > 0 ? ` ${branchIndex} of ${branches.length}` : ""
        }: ${chalk.blue(branchSlug)}`
      );
      branchIndex += 1;
      const url = `${recipeUrl}#${branchSlug}`;
      if (!Array.isArray(features)) {
        continue;
      }
      for (const feature of features) {
        if (
          MESSAGING_EXPERIMENTS_DEFAULT_FEATURES.includes(feature.featureId) &&
          feature.value &&
          typeof feature.value === "object" &&
          feature.value.template
        ) {
          if (!isMessageValid(experimentValidator, feature.value)) {
            console.log(
              `    ${chalk.red(
                "✗"
              )} Skipping invalid value for branch: ${chalk.blue(branchSlug)}`
            );
            continue;
          }
          const messages = (
            feature.value.template === "multi" &&
            Array.isArray(feature.value.messages)
              ? feature.value.messages
              : [feature.value]
          ).filter(m => m && m.id);
          let msgIndex = messages.length > 1 ? 1 : 0;
          for (const message of messages) {
            let messageLogString = `message${
              msgIndex > 0 ? ` ${msgIndex} of ${messages.length}` : ""
            }: ${chalk.italic.green(message.id)}`;
            if (!isMessageValid(messageValidators[message.template], message)) {
              console.log(
                `    ${chalk.red("✗")} Skipping invalid ${messageLogString}`
              );
              continue;
            }
            console.log(`    Importing ${messageLogString}`);
            let slug = `${experimentSlug}:${branchSlug}`;
            if (msgIndex > 0) {
              slug += ` (message ${msgIndex} of ${messages.length})`;
            }
            msgIndex += 1;
            importItems.push({ message, slug, minVersion, maxVersion, url });
          }
        }
      }
    }
  }

  const content = `${LICENSE_STRING}

/**
 * This file is generated by browser/components/asrouter/bin/import-rollouts.js
 * Run the following from the repository root to regenerate it:
 * ./mach npm run import-rollouts --prefix=browser/components/asrouter
 */

export const NimbusRolloutMessageProvider = {
  getMessages() {
    return [${importItems.map(annotateMessage).join(",\n")}];
  },
};
`;

  const formattedContent = await format(content);

  await util.promisify(fs.writeFile)(OUTPUT_PATH, formattedContent);

  console.log(
    `${chalk.green("✓")} Wrote ${chalk.underline.green(
      `${String(importItems.length)} ${
        importItems.length === 1 ? "message" : "messages"
      }`
    )} to ${chalk.underline.yellow(path.resolve(OUTPUT_PATH))}`
  );
}

main();
