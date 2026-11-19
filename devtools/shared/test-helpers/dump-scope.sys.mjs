/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-disable no-console */

import { addDebuggerToGlobal } from "moz-src:///devtools/platform/jsdebugger.sys.mjs";

// Exclude frames from the test harness.
const hiddenSourceURLs = [
  "chrome://mochikit/content/browser-test.js",
  "chrome://mochikit/content/tests/SimpleTest/SimpleTest.js",
  "chrome://remote/content/marionette/",
];

const TYPED_ARRAY_CLASSES = [
  "Uint8Array",
  "Uint8ClampedArray",
  "Uint16Array",
  "Uint32Array",
  "Int8Array",
  "Int16Array",
  "Int32Array",
  "Float32Array",
  "Float64Array",
  "BigInt64Array",
  "BigUint64Array",
];

/**
 * Copied from the similar helper at devtools/server/actors/object/utils.js
 */
function isArray(object) {
  return TYPED_ARRAY_CLASSES.includes(object.class) || object.class === "Array";
}

// Avoid serializing any object more than once by using IDs and refering to them
const USE_REFERENCES = false;

// Avoid serializing more than n-th nested object attributes
const MAX_DEPTH = 5;

// Limit in number of properties/items in a single object
const MAX_PROPERTIES = 100;

// Used by USE_REFERENCES=true to store the already logged objects
const objects = new Map();

const PROMISE_REACTIONS = new WeakMap();
const getAsyncParentFrame = frame => {
  if (!frame.asyncPromise) {
    return null;
  }

  // We support returning Frame actors for frames that are suspended
  // at an 'await', and here we want to walk upward to look for the first
  // frame that will be resumed when the current frame's promise resolves.
  let reactions =
    PROMISE_REACTIONS.get(frame.asyncPromise) ||
    frame.asyncPromise.getPromiseReactions();

  // eslint-disable-next-line no-constant-condition
  while (true) {
    // We loop here because we may have code like:
    //
    //   async function inner(){ debugger; }
    //
    //   async function outer() {
    //     await Promise.resolve().then(() => inner());
    //   }
    //
    // where we can see that when `inner` resolves, we will resume from
    // `outer`, even though there is a layer of promises between, and
    // that layer could be any number of promises deep.
    if (!(reactions[0] instanceof Debugger.Object)) {
      break;
    }

    reactions = reactions[0].getPromiseReactions();
  }

  if (reactions[0] instanceof Debugger.Frame) {
    return reactions[0];
  }
  return null;
};

/**
 * Serialize any arbitrary object to a JSON-serializable object
 */
function serialize(dbgObj, depth) {
  // If the variable is initialized after calling dumpScope.
  if (dbgObj?.uninitialized) {
    return "(uninitialized)";
  }

  // If for any reason SpiderMonkey could not preserve the arguments.
  if (dbgObj?.missingArguments) {
    return "(missing arguments)";
  }

  // If the variable was optimized out by SpiderMonkey.
  if (dbgObj?.optimizedOut) {
    return "(optimized out)";
  }

  if (dbgObj?.unsafeDereference) {
    if (dbgObj.isClassConstructor) {
      return "Class " + dbgObj.name;
    }
    return serializeObject(dbgObj, depth);
  }
  return serializePrimitive(dbgObj);
}

/**
 * Serialize any JavaScript object (i.e. non primitives) to a JSON-serializable object
 */
function serializeObject(dbgObj, depth) {
  depth++;
  if (depth >= MAX_DEPTH) {
    return dbgObj.class + " (max depth)";
  }
  if (dbgObj.class == "Function") {
    return "Function " + dbgObj.displayName;
  }

  let clone = isArray(dbgObj) ? [] : {};
  if (USE_REFERENCES) {
    // Avoid dumping the same object twice by using references
    clone = objects.get(dbgObj);
    if (clone) {
      return "(object #" + clone["object #"] + ")";
    }

    clone["object #"] = objects.size;
    objects.set(dbgObj, clone);
  }

  let i = 0;
  for (const propertyName of dbgObj.getOwnPropertyNames()) {
    const descriptor = dbgObj.getOwnPropertyDescriptor(propertyName);
    if (!descriptor) {
      continue;
    }
    if (i >= MAX_PROPERTIES) {
      clone[propertyName] = "(max properties/items count)";
      break;
    }
    if (descriptor.getter) {
      clone[propertyName] = "(getter)";
    } else {
      clone[propertyName] = serialize(descriptor.value, depth);
    }

    i++;
  }
  return clone;
}

/**
 * Serialize any JavaScript primitive value to a JSON-serializable object
 */
function serializePrimitive(value) {
  const type = typeof value;
  if (type === "string") {
    return value;
  } else if (type === "bigint") {
    return `BigInt(${value})`;
  } else if (value && typeof value.toString === "function") {
    // Use toString as it allows to stringify Symbols. Converting them to string throws.
    return value.toString();
  }

  try {
    // Ensure that the value is really stringifiable
    JSON.stringify(value);
    return value;
  } catch (e) {}

  try {
    // Otherwise we try to stringify it
    return String(value);
  } catch (e) {}
  return "(unserializable: " + type + ")";
}

async function saveAsJsonFile(obj) {
  const jsonString = JSON.stringify(obj, null, 2);
  const encoder = new TextEncoder();
  const jsonBytes = encoder.encode(jsonString);
  if (!Array.isArray(obj) || !obj.length) {
    return;
  }

  // Build a fileName from the last recorded frame information.
  // It should usually match with the actual test file from which the failure
  // was recorded.
  const { columnNumber, frameScriptUrl, lineNumber } = obj.at(-1).details;
  const fileName = [
    frameScriptUrl.substr(frameScriptUrl.lastIndexOf("/") + 1),
    lineNumber,
    columnNumber,
  ].join("_");

  // Add the current timestamp in the filename, it should be impossible to have
  // two failures for the same frame at the same timestamp.
  const hash = Date.now();

  // Save the JSON file either under MOZ_UPLOAD_DIR or under the profile root.
  const filePath = PathUtils.join(
    Services.env.get("MOZ_UPLOAD_DIR") || PathUtils.profileDir,
    `scope-variables-${hash}-${fileName}.json`
  );

  dump(`[dump-scope] Saving scope variables as a JSON file: ${filePath}\n`);

  // Write to file
  await IOUtils.write(filePath, jsonBytes, { compress: false });
}

function serializeFrame(frame) {
  const frameScriptUrl = frame.script.url;
  const { lineNumber, columnNumber } = frame.script.getOffsetMetadata(
    frame.offset
  );
  const frameLocation = `${frameScriptUrl} @ ${lineNumber}:${columnNumber}`;
  dump(`[dump-scope] Serializing variables for frame: ${frameLocation}\n`);

  if (hiddenSourceURLs.some(url => frameScriptUrl?.startsWith(url))) {
    return null;
  }

  const blocks = [];
  const obj = {
    frame: frameLocation,
    // Details will be used to build the filename for the JSON file.
    details: {
      columnNumber,
      frameScriptUrl,
      lineNumber,
    },
    blocks,
  };

  let env = frame.environment;
  while (env && env.type == "declarative" && env.scopeKind != null) {
    const scope = {};
    const names = env.names();
    // Serialize each variable found in the current frame.
    for (const name of names) {
      scope[name] = serialize(env.getVariable(name), 0);
    }
    blocks.push(scope);
    env = env.parent;
  }
  return obj;
}

/**
 * @typedef JSONFrameDetails
 * @property {number} columnNumber
 *     The column number of the exported frame.
 * @property {string} frameScriptUrl
 *     The URL of the script from which the frame was exported.
 * @property {number} lineNumber
 *     The line number of the exported frame.
 */

/**
 * @typedef JSONFrame
 * @property {string} frame
 *     The frame location represented as a string built from the original script
 *     url, the line number and the column number
 * @property {JSONFrameDetails} details
 *     Same information as in frame, but as an object.
 * @property {object} scope
 */

/**
 * The dumpScope helper will attempt to export variables in the current frame
 * and all its ancestor frames to a JSON file that can be stored and inspected
 * later.
 *
 * This is typically intended to be used from tests in continious integration.
 * By default the helper will save all variables in a JSON file stored under
 * MOZ_UPLOAD_DIR if the environment variable is defined, and otherwise saved
 * under the profile root folder.
 *
 * The export will be a best effort snapshot of the variables. The structure
 * of the json file will be an Array of JSONFrame.
 *
 * @param {object} options
 * @param {boolean=} saveAsFile
 *     Set to true to save as a JSON file. Set to false to simply return the
 *     object that would have been stringified to a JSON file.
 */
export const dumpScope = async function ({ saveAsFile = true } = {}) {
  // This will inject `Debugger` in the global scope
  // eslint-disable-next-line mozilla/reject-globalThis-modification
  addDebuggerToGlobal(globalThis);

  const dbg = new Debugger();
  dbg.addAllGlobalsAsDebuggees();

  const scopes = [];
  let frame = dbg.getNewestFrame();
  while (frame) {
    try {
      const scope = serializeFrame(frame);
      if (scope) {
        scopes.push(scope);
      }
    } catch (e) {
      dump("Exception while serializing frame : " + e + "\n");
    }
    frame = frame.older || frame.asyncOlder || getAsyncParentFrame(frame);
  }
  objects.clear();

  if (saveAsFile) {
    return saveAsJsonFile(scopes);
  }

  return scopes;
};
