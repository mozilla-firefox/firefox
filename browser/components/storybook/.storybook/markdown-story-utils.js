/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const path = require("path");
const fs = require("fs");

const projectRoot = path.resolve(__dirname, "../../../../");

/**
 * Takes a file path and returns a string to use as the story title, capitalized
 * and split into multiple words. The file name gets transformed into the story
 * name, which will be visible in the Storybook sidebar. For example, either:
 *
 * /stories/hello-world.stories.md or /stories/helloWorld.md
 *
 * will result in a story named "Hello World".
 *
 * @param {string} filePath - path of the file being processed.
 * @returns {string} The title of the story.
 */
function getTitleFromPath(filePath) {
  let fileName = path.basename(filePath, ".stories.md");
  if (fileName != "README") {
    try {
      let relatedFilePath = path.resolve(
        "../../../",
        filePath.replace(".md", ".mjs")
      );
      let relatedFile = fs.readFileSync(relatedFilePath).toString();
      let relatedTitle = relatedFile.match(/title: "(.*)"/)[1];
      if (relatedTitle) {
        return relatedTitle + "/README";
      }
    } catch {}
  }
  return separateWords(fileName);
}

/**
 * Splits a string into multiple capitalized words e.g. hello-world, helloWorld,
 * and hello.world all become "Hello World."
 *
 * @param {string} str - String in any case.
 * @returns {string} The string split into multiple words.
 */
function separateWords(str) {
  return (
    str
      .match(/[A-Z]?[a-z0-9]+/g)
      ?.map(text => text[0].toUpperCase() + text.substring(1))
      .join(" ") || str
  );
}

/**
 * Enables rendering code in our markdown docs by parsing the source for
 * annotated code blocks and replacing them with live example containers.
 *
 * @param {string} source - Stringified markdown source code.
 * @returns {string} Source with code blocks replaced by example containers.
 */
function parseStoriesFromMarkdown(source) {
  let storiesRegex = /```(?:js|html) story\n(?<code>[\s\S]*?)```/g;
  let exampleStyle = {
    border: "1px solid var(--border-color)",
    borderRadius: "var(--border-radius-small)",
    background: "var(--background-color-canvas)",
    boxShadow: "var(--box-shadow-level-1)",
    overflow: "auto",
  };
  let blockMargin = {
    marginBlock:
      "var(--space-xlarge) calc(var(--space-xlarge) + var(--space-large))",
  };
  let tableCellMargin = { margin: 0 };
  let contentStyle = JSON.stringify({ padding: "var(--space-xxlarge)" });
  // Keep annotated fences as JSX so examples use the same component markup.
  return source.replace(storiesRegex, (match, code, offset, string) => {
    let before = string.slice(0, offset);
    let inTableCell = before.lastIndexOf("<td") > before.lastIndexOf("</td>");
    let style = JSON.stringify({
      ...exampleStyle,
      ...(inTableCell ? tableCellMargin : blockMargin),
    });
    return `<Unstyled style={${style}}><div className="docs-story" style={${contentStyle}}><with-common-styles>\n${code}</with-common-styles></div></Unstyled>`;
  });
}

/**
 * Finds the name of the component for files in toolkit widgets.
 *
 * @param {string} resourcePath - Path to the file being processed.
 * @returns The component name e.g. "moz-toggle"
 */
function getComponentName(resourcePath) {
  let componentName = "";
  if (resourcePath.includes("toolkit/content/widgets")) {
    let storyNameRegex = /(?<=\/widgets\/)(?<name>.*?)(?=\/)/g;
    componentName = storyNameRegex.exec(resourcePath)?.groups?.name;
  }
  return componentName;
}

/**
 * Figures out where a markdown based story should live in Storybook i.e.
 * whether it belongs under "Docs" or "UI Widgets" as well as what name to
 * display in the sidebar.
 *
 * @param {string} resourcePath - Path to the file being processed.
 * @returns {string} The title of the story.
 */
function getStoryTitle(resourcePath) {
  // Currently we sort docs only stories under "Docs" by default.
  let storyPath = "Docs";

  let relativePath = path
    .relative(projectRoot, resourcePath)
    .replaceAll(path.sep, "/");

  let componentName = getComponentName(relativePath);
  if (componentName) {
    // Get the common name for a component e.g. Toggle for moz-toggle
    storyPath =
      "UI Widgets/" + separateWords(componentName).replace(/^Moz/g, "");
  }

  let storyTitle = getTitleFromPath(relativePath);
  let title = storyTitle.includes("/")
    ? storyTitle
    : `${storyPath}/${storyTitle}`;
  return title;
}

/**
 * Figures out the path to import a component for cases where we have
 * interactive examples in the docs that require the component to have been
 * loaded. This wasn't necessary prior to Storybook V7 since everything was
 * loaded up front; now stories are loaded on demand.
 *
 * @param {string} resourcePath - Path to the file being processed.
 * @returns Path used to import a component into a story.
 */
function getImportPath(resourcePath) {
  // We need to normalize the path for this logic to work cross-platform.
  let normalizedPath = resourcePath.split(path.sep).join("/");
  // Limiting this to toolkit widgets for now since we don't have any
  // interactive examples in other docs stories.
  if (!normalizedPath.includes("toolkit/content/widgets")) {
    return "";
  }
  let componentName = getComponentName(normalizedPath);
  let fileExtension = "";
  if (componentName) {
    let mjsPath = normalizedPath.replace(
      "README.stories.md",
      `${componentName}.mjs`
    );
    let jsPath = normalizedPath.replace(
      "README.stories.md",
      `${componentName}.js`
    );

    if (fs.existsSync(mjsPath)) {
      fileExtension = "mjs";
    } else if (fs.existsSync(jsPath)) {
      fileExtension = "js";
    } else {
      return "";
    }
  }
  return `"toolkit-widgets/${componentName}/${componentName}.${fileExtension}"`;
}

/**
 * Takes markdown and re-writes it to MDX. Conditionally includes a table of
 * arguments when we're documenting a component.
 *
 * @param {string} source - The markdown source to rewrite to MDX.
 * @param {string} title - The title of the story.
 * @param {string} resourcePath - Path to the file being processed.
 * @returns The markdown source converted to MDX.
 */
function getMDXSource(source, title, resourcePath = "") {
  let importPath = getImportPath(resourcePath);
  let componentName = getComponentName(resourcePath);

  // Unfortunately the indentation/spacing here seems to be important for the
  // MDX parser to recognize the generated blocks.
  let mdxSource = `
import { Meta, ArgTypes, Unstyled } from "@storybook/addon-docs/blocks";
${importPath ? `import ${importPath};` : ""}

<Meta
  title="${title}"
/>

${parseStoriesFromMarkdown(source)}

${
  importPath &&
  `
## Args Table

<ArgTypes of={"${componentName}"} />
`
}`;

  return mdxSource;
}

module.exports = {
  getMDXSource,
  getStoryTitle,
};
