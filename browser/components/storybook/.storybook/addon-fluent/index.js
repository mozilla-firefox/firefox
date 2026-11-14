/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This file hooks our addon into Storybook. Having a root-level file like this
 * is a Storybook requirement. It handles registering the addon without any
 * additional user configuration.
 */

function previewAnnotations(entry = []) {
  return [...entry, require.resolve("./preset/preview.mjs")];
}

function managerEntries(entry = []) {
  return [...entry, require.resolve("./preset/manager.mjs")];
}

module.exports = {
  managerEntries,
  previewAnnotations,
};
