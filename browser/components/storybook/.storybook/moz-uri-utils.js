/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const path = require("path");
const projectRoot = path.resolve(__dirname, "../../../../");
const [prefixMap, aliasMap, sourceMap] = require("./chrome-map.js").chromeMap;

function rewriteChromeUri(uri) {
  if (uri in aliasMap) {
    return rewriteChromeUri(aliasMap[uri]);
  }
  for (let [prefix, [bundlePath]] of Object.entries(prefixMap)) {
    if (uri.startsWith(prefix)) {
      if (!bundlePath.endsWith("/")) {
        bundlePath += "/";
      }
      let relativePath = uri.slice(prefix.length);
      let objdirPath = bundlePath + relativePath;
      for (let [_objdirPath, [filePath]] of Object.entries(sourceMap)) {
        if (_objdirPath == objdirPath) {
          return path.resolve(projectRoot, filePath);
        }
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
