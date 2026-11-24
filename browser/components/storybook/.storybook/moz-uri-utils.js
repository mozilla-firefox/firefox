/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const fs = require("fs");
const path = require("path");
const projectRoot = path.resolve(__dirname, "../../../../");
const webComponentTokensDir = path.join(
  projectRoot,
  "toolkit/themes/shared/design-system/dist/web/components"
);
const [prefixMap, aliasMap, sourceMap] = require("./chrome-map.js").chromeMap;

/**
 * Returns the directory portion of a path pattern before its first wildcard,
 * with a trailing slash. Patterns without a wildcard are treated as directories.
 *
 * @param {string} pattern
 * @returns {string}
 */
function globBase(pattern) {
  const wildcardIndex = pattern.indexOf("*");
  if (wildcardIndex == -1) {
    return pattern.endsWith("/") ? pattern : `${pattern}/`;
  }
  return pattern.slice(0, pattern.lastIndexOf("/", wildcardIndex) + 1);
}

// jar.mn entries using wildcards (e.g. `(content/**)`) are recorded in the
// chrome map as glob patterns rather than individual files.
const globSourceEntries = Object.entries(sourceMap)
  .filter(([, [filePath]]) => filePath.includes("*"))
  .map(([objdirPath, [filePath]]) => {
    const sourceBase = globBase(filePath);
    return {
      objdirBase: globBase(objdirPath),
      sourceBase,
      pattern: filePath.slice(sourceBase.length),
    };
  })
  .sort((a, b) => b.objdirBase.length - a.objdirBase.length);

function resolveObjdirPath(objdirPath) {
  if (objdirPath in sourceMap) {
    return path.resolve(projectRoot, sourceMap[objdirPath][0]);
  }
  for (let { objdirBase, sourceBase, pattern } of globSourceEntries) {
    if (!objdirPath.startsWith(objdirBase)) {
      continue;
    }
    const relativePath = objdirPath.slice(objdirBase.length);
    // A pattern matching a directory includes everything inside it.
    if (
      path.matchesGlob(relativePath, pattern) ||
      path.matchesGlob(relativePath, `${pattern}/**`)
    ) {
      const resolvedPath = path.resolve(projectRoot, sourceBase + relativePath);
      if (fs.existsSync(resolvedPath)) {
        return resolvedPath;
      }
    }
  }
  return "";
}

function rewriteWebTokensUri(uri) {
  const match = uri.match(
    /^chrome:\/\/global\/content\/elements\/([\w-]+\.tokens\.css)$/
  );
  if (!match) {
    return "";
  }
  const webTokensPath = path.join(webComponentTokensDir, match[1]);
  return fs.existsSync(webTokensPath) ? webTokensPath : "";
}

function rewriteChromeUri(uri) {
  const webTokensPath = rewriteWebTokensUri(uri);
  if (webTokensPath) {
    return webTokensPath;
  }
  if (uri in aliasMap) {
    return rewriteChromeUri(aliasMap[uri]);
  }
  for (let [prefix, [bundlePath]] of Object.entries(prefixMap)) {
    if (uri.startsWith(prefix)) {
      if (!bundlePath.endsWith("/")) {
        bundlePath += "/";
      }
      let relativePath = uri.slice(prefix.length);
      let resolvedPath = resolveObjdirPath(bundlePath + relativePath);
      if (resolvedPath) {
        return resolvedPath;
      }
    }
  }
  return "";
}

function rewriteMozSrcUri(uri) {
  if (!uri.startsWith("moz-src:///")) {
    return "";
  }
  const relativePath = uri.replace(/^moz-src:\/\/\//, "");
  const resolvedPath = path.resolve(projectRoot, relativePath);
  if (!resolvedPath.startsWith(projectRoot)) {
    return "";
  }
  return resolvedPath;
}

module.exports = {
  rewriteChromeUri,
  rewriteMozSrcUri,
};
