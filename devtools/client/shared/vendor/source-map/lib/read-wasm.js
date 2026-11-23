"use strict";

const isNode =
  typeof process !== "undefined" &&
  process.versions != null &&
  process.versions.node != null;

let mappingsWasm = null;

if (isNode) {
  const fs = require("fs");
  const path = require("path");

  module.exports = function readWasm() {
    return new Promise((resolve, reject) => {
      const wasmPath = path.join(__dirname, "mappings.wasm");
      fs.readFile(wasmPath, null, (error, data) => {
        if (error) {
          reject(error);
          return;
        }

        resolve(data.buffer);
      });
    });
  };
} else {
  module.exports = function readWasm() {
    if (typeof mappingsWasm === "string") {
      return fetch(mappingsWasm)
        .then(response => response.arrayBuffer());
    }
    if (mappingsWasm instanceof ArrayBuffer) {
      return Promise.resolve(mappingsWasm);
    }

    throw new Error("You must provide the string URL or ArrayBuffer contents " +
                    "of lib/mappings.wasm by calling " +
                    "SourceMapConsumer.initialize({ 'lib/mappings.wasm': ... }) " +
                    "before using SourceMapConsumer");
  };
}

module.exports.initialize = input => {
  mappingsWasm = input;
};
