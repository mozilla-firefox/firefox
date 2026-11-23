/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

/**
 * Utils for working with Source URLs
 *
 * @module utils/source
 */

const {
  getUnicodeUrl,
} = require("resource://devtools/client/shared/unicode-url.js");
const {
  micromatch,
} = require("resource://devtools/client/shared/vendor/micromatch/micromatch.js");

import { getRelativePath } from "../utils/sources-tree/utils";
import { endTruncateStr } from "./utils";
import { truncateMiddleText } from "../utils/text";
import { memoizeLast } from "../utils/memoizeLast";
import { getEditor } from "./editor/index";
export { isMinified } from "./isMinified";

import { isFulfilled } from "./async-value";

export const sourceTypes = {
  coffee: "coffeescript",
  js: "javascript",
  jsx: "react",
  ts: "typescript",
  tsx: "typescript",
  vue: "vue",
};

export const javascriptLikeExtensions = new Set(["marko", "es6", "vue", "jsm"]);

function getPath(source) {
  const { path } = source.displayURL;
  let lastIndex = path.lastIndexOf("/");
  let nextToLastIndex = path.lastIndexOf("/", lastIndex - 1);

  const result = [];
  do {
    result.push(path.slice(nextToLastIndex + 1, lastIndex));
    lastIndex = nextToLastIndex;
    nextToLastIndex = path.lastIndexOf("/", lastIndex - 1);
  } while (lastIndex !== nextToLastIndex);

  result.push("");

  return result;
}

export function shouldBlackbox(source) {
  if (!source) {
    return false;
  }

  if (!source.url) {
    return false;
  }

  if (source.isStyleSheet) {
    return false;
  }

  return true;
}

/**
 * Checks if the frame is within a line ranges which are blackboxed
 * in the source.
 *
 * @param {object}  frame
 *                  The current frame
 * @param {object}  blackboxedRanges
 *                  The currently blackboxedRanges for all the sources.
 * @param {boolean} isFrameBlackBoxed
 *                  If the frame is within the blackboxed range
 *                  or not.
 */
export function isFrameBlackBoxed(frame, blackboxedRanges) {
  const { source } = frame.location;
  return (
    !!blackboxedRanges[source.url] &&
    (!blackboxedRanges[source.url].length ||
      !!findBlackBoxRange(source, blackboxedRanges, {
        start: frame.location.line,
        end: frame.location.line,
      }))
  );
}

/**
 * Checks if a blackbox range exist for the line range.
 * That is if any start and end lines overlap any of the
 * blackbox ranges
 *
 * @param {object}  source
 *                  The current selected source
 * @param {object}  blackboxedRanges
 *                  The store of blackboxedRanges
 * @param {object}  lineRange
 *                  The start/end line range `{ start: <Number>, end: <Number> }`
 * @return {object} blackboxRange
 *                  The first matching blackbox range that all or part of the
 *                  specified lineRange sits within.
 */
export function findBlackBoxRange(source, blackboxedRanges, lineRange) {
  const ranges = blackboxedRanges[source.url];
  if (!ranges || !ranges.length) {
    return null;
  }

  return ranges.find(
    range =>
      (lineRange.start >= range.start.line &&
        lineRange.start <= range.end.line) ||
      (lineRange.end >= range.start.line && lineRange.end <= range.end.line)
  );
}

/**
 * Checks if a source line is blackboxed
 *
 * @param {Array} ranges - Line ranges that are blackboxed
 * @param {number} line
 * @param {boolean} isSourceOnIgnoreList - is the line in a source that is on
 *                                         the sourcemap ignore lists then the line is blackboxed.
 * @returns boolean
 */
export function isLineBlackboxed(ranges, line, isSourceOnIgnoreList) {
  if (isSourceOnIgnoreList) {
    return true;
  }

  if (!ranges) {
    return false;
  }
  // If the whole source is ignored , then the line is
  // ignored.
  if (!ranges.length) {
    return true;
  }
  return !!ranges.find(
    range => line >= range.start.line && line <= range.end.line
  );
}

/**
 * Returns true if the specified url and/or content type are specific to
 * javascript files.
 *
 * @return boolean
 *         True if the source is likely javascript.
 *
 * @memberof utils/source
 * @static
 */
export function isJavaScript(source, content) {
  const extension = source.displayURL.fileExtension;
  const contentType = content.type === "wasm" ? null : content.contentType;
  return (
    javascriptLikeExtensions.has(extension) ||
    !!(contentType && contentType.includes("javascript"))
  );
}

export function isNotPrettyPrintable(source, sourceContent) {
  return (
    !isJavaScript(source, sourceContent) &&
    !source.isHTML &&
    !source.isStyleSheet
  );
}

export function isPrettyURL(url) {
  return url ? url.endsWith(":formatted") : false;
}

/**
 * @memberof utils/source
 * @static
 */
export function getPrettySourceURL(url) {
  if (!url) {
    url = "";
  }
  return `${url}:formatted`;
}

/**
 * @memberof utils/source
 * @static
 */
export function getRawSourceURL(url) {
  return url && url.endsWith(":formatted")
    ? url.slice(0, -":formatted".length)
    : url;
}

function resolveFileURL(
  url,
  transformUrl = initialUrl => initialUrl,
  truncate = true
) {
  url = getRawSourceURL(url || "");
  const name = transformUrl(url);
  if (!truncate) {
    return name;
  }
  return endTruncateStr(name, 50);
}

export function getFormattedSourceId(id) {
  if (typeof id != "string") {
    console.error(
      "Expected source id to be a string, got",
      typeof id,
      " | id:",
      id
    );
    return "";
  }
  return id.substring(id.lastIndexOf("/") + 1);
}

/**
 * Provides a middle-truncated filename displayed in Tab titles
 */
export function getTruncatedFileName(source) {
  return truncateMiddleText(source.longName, 30);
}

/**
 * Gets path for files with same filename for editor tabs, breakpoints, etc.
 * Pass the source, and list of other sources
 *
 * @memberof utils/source
 * @static
 */

export function getDisplayPath(mySource, sources) {
  const rawSourceURL = getRawSourceURL(mySource.url);
  const filename = mySource.shortName;

  // Find sources that have the same filename, but different paths
  // as the original source
  const similarSources = sources.filter(source => {
    const rawSource = getRawSourceURL(source.url);
    return rawSourceURL != rawSource && filename == source.shortName;
  });

  if (!similarSources.length) {
    return undefined;
  }

  // get an array of source path directories e.g. ['a/b/c.html'] => [['b', 'a']]
  const paths = new Array(similarSources.length + 1);

  paths[0] = getPath(mySource);
  for (let i = 0; i < similarSources.length; ++i) {
    paths[i + 1] = getPath(similarSources[i]);
  }

  // create an array of similar path directories and one dis-similar directory
  // for example [`a/b/c.html`, `a1/b/c.html`] => ['b', 'a']
  // where 'b' is the similar directory and 'a' is the dis-similar directory.
  let displayPath = "";
  for (let i = 0; i < paths[0].length; i++) {
    let similar = false;
    for (let k = 1; k < paths.length; ++k) {
      if (paths[k][i] === paths[0][i]) {
        similar = true;
        break;
      }
    }

    displayPath = paths[0][i] + (i !== 0 ? "/" : "") + displayPath;

    if (!similar) {
      break;
    }
  }

  return displayPath;
}

/**
 * Gets a readable source URL for display purposes.
 * If the source does not have a URL, the source ID will be returned instead.
 *
 * @memberof utils/source
 * @static
 */
export function getFileURL(source, truncate = true) {
  const { url, id } = source;
  if (!url) {
    return getFormattedSourceId(id);
  }

  return resolveFileURL(url, getUnicodeUrl, truncate);
}

function getNthLine(str, lineNum) {
  let startIndex = -1;

  let newLinesFound = 0;
  while (newLinesFound < lineNum) {
    const nextIndex = str.indexOf("\n", startIndex + 1);
    if (nextIndex === -1) {
      return null;
    }
    startIndex = nextIndex;
    newLinesFound++;
  }
  const endIndex = str.indexOf("\n", startIndex + 1);
  if (endIndex === -1) {
    return str.slice(startIndex + 1);
  }

  return str.slice(startIndex + 1, endIndex);
}

export const getLineText = memoizeLast((sourceId, asyncContent, line) => {
  if (!asyncContent || !isFulfilled(asyncContent)) {
    return "";
  }

  const content = asyncContent.value;

  if (content.type === "wasm") {
    const editor = getEditor();
    // The content may belong to a source which isn't displayed in the editor,
    // so map the offset using this content's own WASM state.
    const lines = editor.renderWasmText(content);
    return lines[editor.wasmOffsetToLine(line, content) || 0] || "";
  }

  const lineText = getNthLine(content.value, line - 1);
  return lineText || "";
});

export function getTextAtPosition(sourceId, asyncContent, location) {
  const { column, line = 0 } = location;

  const lineText = getLineText(sourceId, asyncContent, line);
  return lineText.slice(column, column + 100).trim();
}

/**
 * Compute the CSS classname string to use for the icon of a given source.
 *
 * @param {object} source
 *        The reducer source object.
 * @param {boolean} isBlackBoxed
 *        To be set to true, when the given source is blackboxed.
 *        but another tab for that source is opened pretty printed.
 * @return String
 *        The classname to use.
 */
export function getSourceClassnames(source, isBlackBoxed) {
  // Conditionals should be ordered by priority of icon!
  const defaultClassName = "file";

  if (!source || !source.url) {
    return defaultClassName;
  }

  if (isBlackBoxed) {
    return "blackBox";
  }

  if (isUrlExtension(source.url)) {
    return "extension";
  }

  return sourceTypes[source.displayURL.fileExtension] || defaultClassName;
}

export function getRelativeUrl(source, root) {
  const { group, path } = source.displayURL;
  if (!root) {
    return path;
  }

  // + 1 removes the leading "/"
  const url = group + path;
  return url.slice(url.indexOf(root) + root.length + 1);
}

export function isUrlExtension(url) {
  return url.includes("moz-extension:") || url.includes("chrome-extension");
}

/**
* Checks that source url matches one of the glob patterns
*
* @param {object} source
* @param {string} excludePatterns
                  String of comma-seperated glob patterns
* @return {return} Boolean value specifies if the string matches any
                 of the patterns.
*/
export function matchesGlobPatterns(source, excludePatterns) {
  if (!excludePatterns) {
    return false;
  }
  const patterns = excludePatterns
    .split(",")
    .map(pattern => pattern.trim())
    .filter(pattern => pattern !== "");

  if (!patterns.length) {
    return false;
  }

  return micromatch.contains(
    // Makes sure we format the url or id exactly the way its displayed in the search ui,
    // as user wil usually create glob patterns based on what is seen in the ui.
    source.url ? getRelativePath(source.url) : getFormattedSourceId(source.id),
    patterns
  );
}
