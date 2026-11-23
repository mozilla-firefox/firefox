/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Custom element manifest analyzer plugin to remove static and private
 * properties from custom-elements.json that we don't want to document in our
 * Storybook props tables.
 */
function removePrivateAndStaticFields() {
  return {
    packageLinkPhase({ customElementsManifest }) {
      customElementsManifest?.modules?.forEach(m => {
        m?.declarations?.forEach(declaration => {
          if (declaration.members != null) {
            declaration.members = declaration.members.filter(member => {
              return (
                !member.kind === "field" ||
                (!member.static && !member.name.startsWith("#"))
              );
            });
          }
        });
      });
    },
  };
}

/**
 * Custom element manifest config. Controls how we parse directories for custom
 * elements to populate custom-elements.json, which is used by Storybook to
 * generate docs.
 */
const config = {
  globs: ["../../../toolkit/content/widgets/**/*.mjs"],
  exclude: [
    "../../../toolkit/content/widgets/**/*.stories.mjs",
    "../../../toolkit/content/widgets/vendor/**",
  ],
  outdir: ".",
  litelement: true,
  plugins: [removePrivateAndStaticFields()],
};

export default config;
