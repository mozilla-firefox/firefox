/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const fs = require("node:fs");
const path = require("node:path");
const markdownStoryUtils = require("./markdown-story-utils.js");

const { getMDXSource, getStoryTitle } = markdownStoryUtils;

const markdownPattern = /\.(stories|story)\.md$/;
const slash = filePath => filePath.split(path.sep).join("/");

async function findPlugin(plugins, name) {
  for (let plugin of await Promise.all(plugins ?? [])) {
    let found = Array.isArray(plugin)
      ? await findPlugin(plugin, name)
      : plugin?.name === name && plugin;
    if (found) {
      return found;
    }
  }
  return null;
}

/**
 * Serves authored .stories.md files as Storybook docs entries without writing
 * MDX to disk. `indexer` registers the entries and `createPlugin` compiles them
 * with addon-docs when Vite loads them.
 *
 * @param {object} options
 * @param {string} options.configDirectory - Absolute path of the .storybook directory.
 */
function createMarkdownStories({ configDirectory }) {
  const exportDirectory = path.resolve(configDirectory, "../storybook-static");
  const directories = new Set();
  let watcher;

  const indexer = {
    test: markdownPattern,
    async createIndex(fileName, { makeTitle }) {
      let sourceFile = path.resolve(fileName);
      // The static export copies docs back into the story globs.
      if (sourceFile.startsWith(`${exportDirectory}${path.sep}`)) {
        return [];
      }
      let directory = path.dirname(sourceFile);
      directories.add(directory);
      watcher?.add(directory);
      const { Tag } = await import("storybook/internal/core-server");
      // Storybook only turns indexer output into a docs entry through the
      // autodocs tag. That entry has the id of a story named "Docs", so the
      // story entry collapses into it in the index. The MDX tag makes the
      // preview render the default export as MDX.
      return [
        {
          type: "story",
          exportName: "docs",
          title: makeTitle(getStoryTitle(sourceFile)),
          tags: [Tag.AUTODOCS, Tag.UNATTACHED_MDX],
        },
      ];
    },
  };

  async function createPlugin({ plugins, presets }) {
    const mdxPlugin = await findPlugin(plugins, "storybook:mdx-plugin");
    if (!mdxPlugin) {
      throw new Error("@storybook/addon-docs did not register its MDX plugin");
    }
    const { handler } = mdxPlugin.transform;

    return {
      name: "firefox-markdown-docs",
      enforce: "pre",
      transform(source, id) {
        if (!markdownPattern.test(id)) {
          return null;
        }
        let sourceFile = path.resolve(id);
        // The addon only compiles ids ending in .mdx.
        return handler.call(
          this,
          getMDXSource(source, getStoryTitle(sourceFile), slash(sourceFile)),
          `${id}.mdx`
        );
      },
      async configureServer(server) {
        const generator = await presets.apply("storyIndexGenerator");
        const moduleGraph = server.environments.client.moduleGraph;
        watcher = server.watcher;
        // Sibling stories supply titles, and component files supply imports.
        watcher.on("all", (event, file) => {
          if (
            !["add", "change", "unlink"].includes(event) ||
            !/\.m?js$/.test(file)
          ) {
            return;
          }
          let directory = path.dirname(path.resolve(file));
          if (!directories.has(directory) || !fs.existsSync(directory)) {
            return;
          }
          for (let name of fs.readdirSync(directory)) {
            if (!markdownPattern.test(name)) {
              continue;
            }
            let markdownFile = path.join(directory, name);
            let importPath = slash(path.relative(process.cwd(), markdownFile));
            generator.invalidate(
              importPath.startsWith(".") ? importPath : `./${importPath}`,
              false
            );
            for (let node of moduleGraph.getModulesByFile(
              slash(markdownFile)
            ) ?? []) {
              moduleGraph.invalidateModule(node);
            }
          }
        });
        watcher.add([...directories]);
      },
    };
  }

  return { indexer, createPlugin };
}

module.exports = { createMarkdownStories };
