/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { readFileSync } = require("fs");

/**
 * Creates a mock of the platform Localization class, reading en-US FTL files
 * from the source tree.
 *
 * @param {object} fluentBundle
 *        The @fluent/bundle module, which must be required from the test suite
 *        folder, as this file doesn't have access to its node_modules.
 * @return {Function}
 *         The Localization class.
 */
function createLocalizationMock({ FluentBundle, FluentResource }) {
  return class Localization {
    constructor(resourceIds) {
      // Gecko doesn't insert bidi isolation marks around placeables.
      this.bundle = new FluentBundle("en-US", { useIsolating: false });
      for (const resourceId of resourceIds) {
        // e.g. "devtools/shared/foo.ftl" -> "devtools/shared/locales/en-US/foo.ftl"
        const index = resourceId.lastIndexOf("/");
        const sourcePath = `${resourceId.substring(0, index)}/locales/en-US${resourceId.substring(index)}`;
        let filePath;
        try {
          // devtools/ paths are mapped to the source tree by the Jest config.
          filePath = require.resolve(sourcePath);
        } catch (e) {
          continue;
        }
        this.bundle.addResource(
          new FluentResource(readFileSync(filePath, { encoding: "utf8" }))
        );
      }
    }

    formatValueSync(id, args) {
      const message = this.bundle.getMessage(id);
      if (!message?.value) {
        return null;
      }
      return this.bundle.formatPattern(message.value, args);
    }

    formatMessagesSync(keys) {
      return keys.map(({ id, args }) => {
        const message = this.bundle.getMessage(id);
        if (!message) {
          return null;
        }
        return {
          value: message.value
            ? this.bundle.formatPattern(message.value, args)
            : null,
          attributes: Object.entries(message.attributes).map(
            ([name, value]) => ({
              name,
              value: this.bundle.formatPattern(value, args),
            })
          ),
        };
      });
    }
  };
}

module.exports = { createLocalizationMock };
