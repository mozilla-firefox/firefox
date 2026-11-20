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
