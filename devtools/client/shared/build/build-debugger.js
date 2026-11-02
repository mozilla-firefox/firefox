/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

"use strict";

// eslint-disable-next-line mozilla/reject-relative-requires
const Babel = require("./babel");
const _path = require("path");

function isRequire(t, node) {
  return node && t.isCallExpression(node) && node.callee.name == "require";
}

function shouldLazyLoad(value) {
  return (
    !value.includes("codemirror/") &&
    !value.endsWith(".properties") &&
    !value.startsWith("devtools/") &&
    !value.startsWith("resource://devtools/")
  );
}

/**
 * This Babel plugin is used to transpile a single Debugger module into a module that
 * can be loaded in Firefox via the regular DevTools loader.
 */
function transformMC({ types: t }) {
  return {
    visitor: {
      StringLiteral(path, state) {
        const { filePath } = state.opts;
        let value = path.node.value;

        if (!isRequire(t, path.parent)) {
          return;
        }

        if (shouldLazyLoad(value)) {
          const requireCall = path.parentPath;
          const declarator = requireCall.parentPath;
          const declaration = declarator.parentPath;

          // require()s that are not assigned to a variable cannot be safely lazily required
          // since we lack anything to initiate the require (= the getter for the variable)
          if (declarator.type !== "VariableDeclarator") {
            return;
          }

          // update relative paths to be "absolute" (starting with devtools/)
          // e.g. ./utils/source-queue
          if (value.startsWith(".")) {
            // Create full path
            // e.g. z:\build\build\src\devtools\client\debugger\src\utils\source-queue
            let newValue = _path.join(_path.dirname(filePath), value);

            // Select the devtools portion of the path
            // e.g. devtools\client\debugger\src\utils\source-queue
            if (!newValue.startsWith("devtools")) {
              const m = newValue.match(/.*(devtools.*)/);
              if (!m) {
                return;
              }
              newValue = m[1];
            }

            // Replace forward slashes with back slashes
            // e.g devtools/client/debugger/src/utils/source-queue
            newValue = newValue.replace(/\\/g, "/");

            value = newValue;
          }

          // rewrite to: loader.lazyRequireGetter(this, "variableName", "pathToFile")
          const lazyRequire = t.callExpression(
            t.memberExpression(
              t.identifier("loader"),
              t.identifier("lazyRequireGetter")
            ),
            [
              t.thisExpression(),
              t.stringLiteral(declarator.node.id.name || ""),
              t.stringLiteral(value),
            ]
          );

          declaration.replaceWith(lazyRequire);
        }
      },
    },
  };
}

Babel.registerPlugin("transform-mc", transformMC);

module.exports = function (filePath) {
  return [
    "proposal-class-properties",
    "transform-modules-commonjs",
    ["transform-mc", { filePath }],
  ];
};
