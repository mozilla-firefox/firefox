/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import path from "node:path";
import { fileURLToPath } from "node:url";
import { mergeConfig, normalizePath, transformWithOxc } from "vite";
import markdownStoryGenerator from "./markdown-story-generator.js";
import { createMozillaResourcePlugin } from "./moz-vite-plugin.mjs";

const configDirectory = path.dirname(fileURLToPath(import.meta.url));
const projectRoot = normalizePath(
  path.resolve(configDirectory, "../../../../")
);
const markdownStories = markdownStoryGenerator.createMarkdownStories({
  configDirectory,
});

// The ordering for this stories array affects the order that they are displayed in Storybook
const storyPatterns = [
  // Show the Storybook document first in the list
  // so that navigating to firefoxux.github.io/firefox-desktop-components/
  // lands on the ComponentStatus.stories.md file
  `../**/component-status.stories.mjs`,
  // and lands on the Storybook.stories.md file
  "../**/README.storybook.stories.md",
  // Docs section
  "../**/README.*.stories.md",
  // UI Widgets section
  `${projectRoot}/toolkit/content/widgets/**/*.stories.@(js|jsx|mjs|ts|tsx|md)`,
  // about:logins components stories
  `${projectRoot}/browser/components/aboutlogins/content/components/**/*.stories.mjs`,
  // ASRouter components stories
  `${projectRoot}/browser/components/asrouter/content/**/*.stories.mjs`,
  // Backup components stories
  `${projectRoot}/browser/components/backup/content/**/*.stories.mjs`,
  // Settings components stories
  `${projectRoot}/browser/components/preferences/widgets/**/*.stories.mjs`,
  // Search components stories
  `${projectRoot}/browser/components/search/**/*.stories.mjs`,
  // Reader View components stories
  `${projectRoot}/toolkit/components/reader/**/*.stories.mjs`,
  // Password Manager doorhanger components stories
  `${projectRoot}/browser/components/passwordmgr/content/**/*.stories.mjs`,
  // megalist components stories
  `${projectRoot}/toolkit/components/satchel/megalist/content/**/*.stories.mjs`,
  // autocomplete-row-item stories
  `${projectRoot}/toolkit/components/satchel/autocomplete-row-item/**/*.stories.@(mjs|md)`,
  // WebRTC components stories
  `${projectRoot}/browser/components/webrtc/content/**/*.stories.mjs`,
  // AI Window components stories
  `${projectRoot}/browser/components/aiwindow/ui/**/*.stories.mjs`,
  // Genai components stories
  `${projectRoot}/browser/components/genai/content/**/*.stories.mjs`,
  // Multiline editor components stories
  `${projectRoot}/browser/components/multilineeditor/**/*.stories.@(mjs|md)`,
  // Trust Panel components stories
  `${projectRoot}/browser/components/controlcenter/content/components/**/*.stories.mjs`,
  // Widget components stories
  `${projectRoot}/browser/components/widgets/content/**/*.stories.@(mjs|md)`,
  // Everything else
  "../stories/**/*.stories.@(js|jsx|mjs|ts|tsx|md)",
  // Design system files
  `${projectRoot}/toolkit/themes/shared/design-system/**/*.stories.@(js|jsx|mjs|ts|tsx|md)`,
];

export default {
  stories: storyPatterns,
  experimental_indexers: existingIndexers => [
    ...existingIndexers,
    markdownStories.indexer,
  ],
  staticDirs: [
    `${projectRoot}/toolkit/themes/shared/design-system/docs/`,
    // This allows static images to be correctly served when placed in
    // a folder outside of the design-system/docs/ folder
    {
      from: `${projectRoot}/browser/components/storybook/docs/img`,
      to: "/img",
    },
  ],
  addons: [
    "@storybook/addon-themes",
    "@storybook/addon-links",
    "@storybook/addon-docs",
    "@storybook/addon-a11y",
    path.resolve(configDirectory, "addon-fluent/index.js"),
    path.resolve(configDirectory, "addon-component-status/index.js"),
  ],
  features: { backgrounds: false, measure: false, outline: false },
  framework: {
    name: "@storybook/web-components-vite",
    options: {},
  },
  async viteFinal(config, options) {
    const markdownPlugin = await markdownStories.createPlugin({
      plugins: config.plugins,
      presets: options.presets,
    });
    return mergeConfig(config, {
      resolve: {
        alias: {
          browser: `${projectRoot}/browser`,
          third_party: `${projectRoot}/third_party`,
          toolkit: `${projectRoot}/toolkit`,
          "toolkit-widgets": `${projectRoot}/toolkit/content/widgets/`,
          "lit.all.mjs": `${projectRoot}/toolkit/content/widgets/vendor/lit.all.mjs`,
          "react/jsx-runtime": fileURLToPath(
            import.meta.resolve("react/jsx-runtime")
          ),
          react: path.join(configDirectory, "../node_modules/react"),
          "@storybook/addon-docs/blocks": fileURLToPath(
            import.meta.resolve("@storybook/addon-docs/blocks")
          ),
          "@storybook/addon-docs": fileURLToPath(
            import.meta.resolve("@storybook/addon-docs")
          ),
        },
      },
      server: { fs: { allow: [projectRoot] } },
      plugins: [
        {
          name: "firefox-storybook-jsx",
          enforce: "pre",
          transform(source, id) {
            const file = normalizePath(id.split("?", 1)[0]);
            if (
              !file.startsWith(`${normalizePath(configDirectory)}/`) ||
              !file.endsWith(".mjs")
            ) {
              return null;
            }
            return transformWithOxc(source, file, {
              lang: "jsx",
              jsx: { runtime: "classic" },
            });
          },
        },
        markdownPlugin,
        createMozillaResourcePlugin({ projectRoot }),
      ],
    });
  },
};
