const readWasm = require("../lib/read-wasm");

/**
 * Provide the JIT with a nice shape / hidden class.
 */
function Mapping() {
  this.generatedLine = 0;
  this.generatedColumn = 0;
  this.lastGeneratedColumn = null;
  this.source = null;
  this.originalLine = null;
  this.originalColumn = null;
  this.name = null;
}

/**
 * Wrap a `mapping => {}` callback in the flat-argument form the wasm module
 * invokes. Building the `Mapping` here (rather than in Rust) keeps every
 * mapping on a single hidden class.
 */
function mappingCallback(callback) {
  return function (
    generatedLine,
    generatedColumn,

    hasLastGeneratedColumn,
    lastGeneratedColumn,

    hasOriginal,
    source,
    originalLine,
    originalColumn,

    hasName,
    name
  ) {
    const mapping = new Mapping();
    // JS uses 1-based line numbers, wasm uses 0-based.
    mapping.generatedLine = generatedLine + 1;
    mapping.generatedColumn = generatedColumn;

    if (hasLastGeneratedColumn) {
      // JS uses inclusive last generated column, wasm uses exclusive.
      mapping.lastGeneratedColumn = lastGeneratedColumn - 1;
    }

    if (hasOriginal) {
      mapping.source = source;
      // JS uses 1-based line numbers, wasm uses 0-based.
      mapping.originalLine = originalLine + 1;
      mapping.originalColumn = originalColumn;

      if (hasName) {
        mapping.name = name;
      }
    }

    callback(mapping);
  };
}

let cachedWasm = null;

module.exports = function wasm() {
  if (cachedWasm) {
    return cachedWasm;
  }

  cachedWasm = readWasm().then(null, e => {
    cachedWasm = null;
    throw e;
  });

  return cachedWasm;
};

module.exports.mappingCallback = mappingCallback;
