/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { DevToolsServer } = require("resource://devtools/server/devtools-server.js");
const DevToolsUtils = require("resource://devtools/shared/DevToolsUtils.js");
loader.lazyRequireGetter(
  this,
  "ObjectUtils",
  "resource://devtools/server/actors/object/utils.js"
);
loader.lazyRequireGetter(
  this,
  "PropertyIterators",
  "resource://devtools/server/actors/object/property-iterator.js"
);
loader.lazyRequireGetter(
  this,
  "propertyDescriptor",
  "resource://devtools/server/actors/object/property-descriptor.js",
  true
);

// Number of items to preview in objects, arrays, maps, sets, lists,
// collections, etc.
const OBJECT_PREVIEW_MAX_ITEMS = 10;

const ERROR_CLASSNAMES = new Set([
  "Error",
  "EvalError",
  "RangeError",
  "ReferenceError",
  "SyntaxError",
  "TypeError",
  "URIError",
  "InternalError",
  "AggregateError",
  "CompileError",
  "DebuggeeWouldRun",
  "LinkError",
  "RuntimeError",
  "Exception", // This related to Components.Exception()
  "SuppressedError",
]);
const ARRAY_LIKE_CLASSNAMES = new Set([
  "DOMStringList",
  "DOMTokenList",
  "CSSRuleList",
  "MediaList",
  "StyleSheetList",
  "NamedNodeMap",
  "FileList",
  "NodeList",
]);
const OBJECT_WITH_URL_CLASSNAMES = new Set([
  "CSSImportRule",
  "CSSStyleSheet",
  "Location",
  "TrustedScriptURL"
]);

/**
 * Functions for adding information to ObjectActor grips for the purpose of
 * having customized output. This object holds arrays mapped by
 * Debugger.Object.prototype.class.
 *
 * In each array you can add functions that take three
 * arguments:
 *   - the ObjectActor instance and its hooks to make a preview for,
 *   - the grip object being prepared for the client,
 *   - the depth of the object compared to the top level object,
 *     when we are inspecting nested attributes.
 *
 * Functions must return false if they cannot provide preview
 * information for the debugger object, or true otherwise.
 */
const previewers = {
  String: [
    function(objectActor, grip, depth) {
      return wrappedPrimitivePreviewer(
        String,
        objectActor,
        grip,
        depth
      );
    },
  ],

  Boolean: [
    function(objectActor, grip, depth) {
      return wrappedPrimitivePreviewer(
        Boolean,
        objectActor,
        grip,
        depth
      );
    },
  ],

  Number: [
    function(objectActor, grip, depth) {
      return wrappedPrimitivePreviewer(
        Number,
        objectActor,
        grip,
        depth
      );
    },
  ],

  Symbol: [
    function(objectActor, grip, depth) {
      return wrappedPrimitivePreviewer(
        Symbol,
        objectActor,
        grip,
        depth
      );
    },
  ],

  Function: [
    function(objectActor, grip, depth) {
      const { obj } = objectActor;
      if (obj.name) {
        grip.name = obj.name;
      }

      if (obj.displayName) {
        grip.displayName = obj.displayName.substr(0, 500);
      }

      if (obj.parameterNames) {
        grip.parameterNames = obj.parameterNames;
      }

      // Check if the developer has added a de-facto standard displayName
      // property for us to use.
      let userDisplayName;
      try {
        userDisplayName = obj.getOwnPropertyDescriptor("displayName");
      } catch (e) {
        // The above can throw "permission denied" errors when the debuggee
        // does not subsume the function's compartment.
      }

      if (
        userDisplayName &&
        typeof userDisplayName.value == "string" &&
        userDisplayName.value
      ) {
        grip.userDisplayName = objectActor.createValueGrip(userDisplayName.value, depth);
      }

      grip.isAsync = obj.isAsyncFunction;
      grip.isGenerator = obj.isGeneratorFunction;

      if (obj.script) {
        // NOTE: Debugger.Script.prototype.startColumn is 1-based.
        //       Convert to 0-based, while keeping the wasm's column (1) as is.
        //       (bug 1863878)
        const columnBase = obj.script.format === "wasm" ? 0 : 1;
        grip.location = {
          url: obj.script.url,
          line: obj.script.startLine,
          column: obj.script.startColumn - columnBase,
        };
      }

      return true;
    },
  ],

  RegExp: [
    function(objectActor, grip, depth) {
      let str;
      if (isWorker) {
        // For some reason, the following incantation on the worker thread returns "/undefined/undefined"
        // str = RegExp.prototype.toString.call(objectActor.obj.unsafeDereference());
        //
        // The following method will throw in case of method being overloaded by the page,
        // and a more generic previewer will render the object.
        try {
          str = DevToolsUtils.callPropertyOnObject(objectActor.obj, "toString");
        } catch(e) {
          // Ensure displaying something in case of error.
          // Otherwise this would render an object with an empty label
          grip.displayString = "RegExp with overloaded toString";
        }
      } else {
        const { RegExp } = objectActor.targetActor.targetGlobal;
        str = RegExp.prototype.toString.call(objectActor.safeRawObj);
      }

      if (typeof str != "string") {
        return false;
      }

      grip.displayString = objectActor.createValueGrip(str, depth);
      return true;
    },
  ],

  Date: [
    function(objectActor, grip, depth) {
      let time;
      if (isWorker) {
        // Also, targetGlobal is an opaque wrapper, from which we can't access its Date object,
        // so fallback to the privileged one
        //
        // In worker objectActor.safeRawObj is considered unsafe and is null,
        // so retrieve the objectActor.rawObj object directly from Debugger.Object.unsafeDereference
        time = Date.prototype.getTime.call(objectActor.rawObj);
      } else {
        const { Date } = objectActor.targetActor.targetGlobal;
        time = Date.prototype.getTime.call(objectActor.safeRawObj);
      }
      if (typeof time != "number") {
        return false;
      }

      grip.preview = {
        timestamp: objectActor.createValueGrip(time, depth),
      };
      return true;
    },
  ],

  "Temporal.Instant": [
    function(objectActor, grip, depth) {
      temporalPreviewer(Temporal.Instant, objectActor, grip);
      return true;
    },
  ],

  "Temporal.PlainDate": [
    function(objectActor, grip, depth) {
      temporalPreviewer(Temporal.PlainDate, objectActor, grip);
      return true;
    },
  ],

  "Temporal.PlainDateTime": [
    function(objectActor, grip, depth) {
      temporalPreviewer(Temporal.PlainDateTime, objectActor, grip);
      return true;
    },
  ],

  "Temporal.PlainDate": [
    function(objectActor, grip, depth) {
      temporalPreviewer(Temporal.PlainDate, objectActor, grip);
      return true;
    },
  ],

  "Temporal.PlainMonthDay": [
    function(objectActor, grip, depth) {
      temporalPreviewer(Temporal.PlainMonthDay, objectActor, grip);
      return true;
    },
  ],

  "Temporal.PlainTime": [
    function(objectActor, grip, depth) {
      temporalPreviewer(Temporal.PlainTime, objectActor, grip);
      return true;
    },
  ],

  "Temporal.PlainYearMonth": [
    function(objectActor, grip, depth) {
      temporalPreviewer(Temporal.PlainYearMonth, objectActor, grip);
      return true;
    },
  ],

  "Temporal.ZonedDateTime": [
    function(objectActor, grip, depth) {
      temporalPreviewer(Temporal.ZonedDateTime, objectActor, grip);
      return true;
    },
  ],

  "Temporal.Duration": [
    function(objectActor, grip, depth) {
      temporalPreviewer(Temporal.Duration, objectActor, grip);
      return true;
    },
  ],

  TrustedHTML: [
    function(objectActor, grip, depth) {
      const text = TrustedHTML.prototype.toString.call(
        // In worker objectActor.safeRawObj is considered unsafe and is null
        objectActor.safeRawObj || objectActor.rawObj
      );

      grip.preview = {
        kind: "ObjectWithText",
        text: objectActor.createValueGrip(text, depth)
      };
      return true;
    },
  ],

  TrustedScript: [
    function(objectActor, grip, depth) {
      const text = TrustedScript.prototype.toString.call(
        // In worker objectActor.safeRawObj is considered unsafe and is null
        objectActor.safeRawObj || objectActor.rawObj
      );

      grip.preview = {
        kind: "ObjectWithText",
        text: objectActor.createValueGrip(text, depth)
      };
      return true;
    },
  ],

  TrustedScriptURL: [
    function(objectActor, grip, depth) {
      const url = TrustedScriptURL.prototype.toString.call(
        // In worker objectActor.safeRawObj is considered unsafe and is null
        objectActor.safeRawObj || objectActor.rawObj
      );

      grip.preview = {
        kind: "ObjectWithURL",
        url: objectActor.createValueGrip(url, depth)
      };
      return true;
    },
  ],

  Array: [
    function(objectActor, grip, depth) {
      const length = ObjectUtils.getArrayLength(objectActor.obj);

      grip.preview = {
        kind: "ArrayLike",
        length: length,
      };

      if (depth > 1) {
        return true;
      }

      const { obj, rawObj } = objectActor;
      const items = (grip.preview.items = []);

      for (let i = 0; i < length; ++i) {
        if (rawObj && !isWorker) {
          // Array Xrays filter out various possibly-unsafe properties (like
          // functions, and claim that the value is undefined instead. This
          // is generally the right thing for privileged code accessing untrusted
          // objects, but quite confusing for Object previews. So we manually
          // override this protection by waiving Xrays on the array, and re-applying
          // Xrays on any indexed value props that we pull off of it.
          const desc = Object.getOwnPropertyDescriptor(Cu.waiveXrays(rawObj), i);
          if (desc && !desc.get && !desc.set) {
            let value = Cu.unwaiveXrays(desc.value);
            value = ObjectUtils.makeDebuggeeValueIfNeeded(obj, value);
            items.push(objectActor.createValueGrip(value, depth));
          } else if (!desc) {
            items.push(null);
          } else {
            const item = {};
            if (desc.get) {
              let getter = Cu.unwaiveXrays(desc.get);
              getter = ObjectUtils.makeDebuggeeValueIfNeeded(obj, getter);
              item.get = objectActor.createValueGrip(getter, depth);
            }
            if (desc.set) {
              let setter = Cu.unwaiveXrays(desc.set);
              setter = ObjectUtils.makeDebuggeeValueIfNeeded(obj, setter);
              item.set = objectActor.createValueGrip(setter, depth);
            }
            items.push(item);
          }
        } else if (rawObj && !obj.getOwnPropertyDescriptor(i)) {
          items.push(null);
        } else {
          // Workers do not have access to Cu.
          const value = DevToolsUtils.getProperty(obj, i);
          items.push(objectActor.createValueGrip(value, depth));
        }

        if (items.length == OBJECT_PREVIEW_MAX_ITEMS) {
          break;
        }
      }

      return true;
    },
  ],

  Set: [
    function(objectActor, grip, depth) {
      const size = DevToolsUtils.getProperty(objectActor.obj, "size");
      if (typeof size != "number") {
        return false;
      }

      grip.preview = {
        kind: "ArrayLike",
        length: size,
      };

      // Avoid recursive object grips.
      if (depth > 1) {
        return true;
      }

      const items = (grip.preview.items = []);
      for (const item of PropertyIterators.enumSetEntries(objectActor, depth)) {
        items.push(item);
        if (items.length == OBJECT_PREVIEW_MAX_ITEMS) {
          break;
        }
      }

      return true;
    },
  ],

  WeakSet: [
    function(objectActor, grip, depth) {
      const enumEntries = PropertyIterators.enumWeakSetEntries(objectActor, depth);

      grip.preview = {
        kind: "ArrayLike",
        length: enumEntries.size,
      };

      // Avoid recursive object grips.
      if (depth > 1) {
        return true;
      }

      const items = (grip.preview.items = []);
      for (const item of enumEntries) {
        items.push(item);
        if (items.length == OBJECT_PREVIEW_MAX_ITEMS) {
          break;
        }
      }

      return true;
    },
  ],

  Map: [
    function(objectActor, grip, depth) {
      const size = DevToolsUtils.getProperty(objectActor.obj, "size");
      if (typeof size != "number") {
        return false;
      }

      grip.preview = {
        kind: "MapLike",
        size: size,
      };

      if (depth > 1) {
        return true;
      }

      const entries = (grip.preview.entries = []);
      for (const entry of PropertyIterators.enumMapEntries(objectActor, depth)) {
        entries.push(entry);
        if (entries.length == OBJECT_PREVIEW_MAX_ITEMS) {
          break;
        }
      }

      return true;
    },
  ],

  WeakMap: [
    function(objectActor, grip, depth) {
      const enumEntries = PropertyIterators.enumWeakMapEntries(objectActor, depth);

      grip.preview = {
        kind: "MapLike",
        size: enumEntries.size,
      };

      if (depth > 1) {
        return true;
      }

      const entries = (grip.preview.entries = []);
      for (const entry of enumEntries) {
        entries.push(entry);
        if (entries.length == OBJECT_PREVIEW_MAX_ITEMS) {
          break;
        }
      }

      return true;
    },
  ],

  URLSearchParams: [
    function(objectActor, grip, depth) {
      const enumEntries = PropertyIterators.enumURLSearchParamsEntries(objectActor, depth);

      grip.preview = {
        kind: "MapLike",
        size: enumEntries.size,
      };

      if (depth > 1) {
        return true;
      }

      const entries = (grip.preview.entries = []);
      for (const entry of enumEntries) {
        entries.push(entry);
        if (entries.length == OBJECT_PREVIEW_MAX_ITEMS) {
          break;
        }
      }

      return true;
    },
  ],

  FormData: [
    function(objectActor, grip, depth) {
      const enumEntries = PropertyIterators.enumFormDataEntries(objectActor, depth);

      grip.preview = {
        kind: "MapLike",
        size: enumEntries.size,
      };

      if (depth > 1) {
        return true;
      }

      const entries = (grip.preview.entries = []);
      for (const entry of enumEntries) {
        entries.push(entry);
        if (entries.length == OBJECT_PREVIEW_MAX_ITEMS) {
          break;
        }
      }

      return true;
    },
  ],

  Headers: [
    function(objectActor, grip, depth) {
      // Bug 1863776: Headers can't be yet previewed from workers
      if (isWorker) {
        return false;
      }
      const enumEntries = PropertyIterators.enumHeadersEntries(objectActor, depth);

      grip.preview = {
        kind: "MapLike",
        size: enumEntries.size,
      };

      if (depth > 1) {
        return true;
      }

      const entries = (grip.preview.entries = []);
      for (const entry of enumEntries) {
        entries.push(entry);
        if (entries.length == OBJECT_PREVIEW_MAX_ITEMS) {
          break;
        }
      }

      return true;
    },
  ],


  HighlightRegistry: [
    function(objectActor, grip, depth) {
      const enumEntries = PropertyIterators.enumHighlightRegistryEntries(objectActor, depth);

      grip.preview = {
        kind: "MapLike",
        size: enumEntries.size,
      };

      if (depth > 1) {
        return true;
      }

      const entries = (grip.preview.entries = []);
      for (const entry of enumEntries) {
        entries.push(entry);
        if (entries.length == OBJECT_PREVIEW_MAX_ITEMS) {
          break;
        }
      }

      return true;
    },
  ],

  MIDIInputMap: [
    function(objectActor, grip, depth) {
      const enumEntries = PropertyIterators.enumMidiInputMapEntries(
        objectActor,
        depth
      );

      grip.preview = {
        kind: "MapLike",
        size: enumEntries.size,
      };

      if (depth > 1) {
        return true;
      }

      const entries = (grip.preview.entries = []);
      for (const entry of enumEntries) {
        entries.push(entry);
        if (entries.length == OBJECT_PREVIEW_MAX_ITEMS) {
          break;
        }
      }

      return true;
    },
  ],

  MIDIOutputMap: [
    function(objectActor, grip, depth) {
      const enumEntries = PropertyIterators.enumMidiOutputMapEntries(
        objectActor,
        depth
      );

      grip.preview = {
        kind: "MapLike",
        size: enumEntries.size,
      };

      if (depth > 1) {
        return true;
      }

      const entries = (grip.preview.entries = []);
      for (const entry of enumEntries) {
        entries.push(entry);
        if (entries.length == OBJECT_PREVIEW_MAX_ITEMS) {
          break;
        }
      }

      return true;
    },
  ],

  DOMStringMap: [
    function(objectActor, grip, depth) {
      const { obj, safeRawObj } = objectActor;
      if (!safeRawObj) {
        return false;
      }

      const keys = obj.getOwnPropertyNames();
      grip.preview = {
        kind: "MapLike",
        size: keys.length,
      };

      if (depth > 1) {
        return true;
      }

      const entries = (grip.preview.entries = []);
      for (const key of keys) {
        const value = ObjectUtils.makeDebuggeeValueIfNeeded(obj, safeRawObj[key]);
        entries.push([key, objectActor.createValueGrip(value, depth)]);
        if (entries.length == OBJECT_PREVIEW_MAX_ITEMS) {
          break;
        }
      }

      return true;
    },
  ],

  Promise: [
    function(objectActor, grip, depth) {
      const { state, value, reason } = ObjectUtils.getPromiseState(objectActor.obj);
      const ownProperties = Object.create(null);
      ownProperties["<state>"] = { value: state };
      let ownPropertiesLength = 1;

      // Only expose <value> or <reason> in top-level promises, to avoid recursion.
      // <state> is not problematic because it's a string.
      if (depth === 1) {
        if (state == "fulfilled") {
          ownProperties["<value>"] = { value: objectActor.createValueGrip(value, depth) };
          ++ownPropertiesLength;
        } else if (state == "rejected") {
          ownProperties["<reason>"] = { value: objectActor.createValueGrip(reason, depth) };
          ++ownPropertiesLength;
        }
      }

      grip.preview = {
        kind: "Object",
        ownProperties,
        ownPropertiesLength,
      };

      return true;
    },
  ],

  Proxy: [
    function(objectActor, grip, depth) {
      // Only preview top-level proxies, avoiding recursion. Otherwise, since both the
      // target and handler can also be proxies, we could get an exponential behavior.
      if (depth > 1) {
        return true;
      }

      const { obj } = objectActor;

      // The `isProxy` getter of the debuggee object only detects proxies without
      // security wrappers. If false, the target and handler are not available.
      const hasTargetAndHandler = obj.isProxy;

      grip.preview = {
        kind: "Object",
        ownProperties: Object.create(null),
        ownPropertiesLength: 2 * hasTargetAndHandler,
      };

      if (hasTargetAndHandler) {
        Object.assign(grip.preview.ownProperties, {
          "<target>": { value: objectActor.createValueGrip(obj.proxyTarget, depth) },
          "<handler>": { value: objectActor.createValueGrip(obj.proxyHandler, depth) },
        });
      }

      return true;
    },
  ],

  CustomStateSet: [
    function(objectActor, grip, depth) {
      const size = DevToolsUtils.getProperty(objectActor.obj, "size");
      if (typeof size != "number") {
        return false;
      }

      grip.preview = {
        kind: "ArrayLike",
        length: size,
      };

      const items = (grip.preview.items = []);
      for (const item of PropertyIterators.enumCustomStateSetEntries(objectActor, depth)) {
        items.push(item);
        if (items.length == OBJECT_PREVIEW_MAX_ITEMS) {
          break;
        }
      }

      return true;
    },
  ],
};

/**
 * Generic previewer for classes wrapping primitives, like String,
 * Number and Boolean.
 *
 * @param object classObj
 *        The class to expect, eg. String. The valueOf() method of the class is
 *        invoked on the given object.
 * @param ObjectActor objectActor
 *        The object actor
 * @param Object grip
 *        The result grip to fill in
 * @param Number depth
 *        Depth of the object compared to the top level object,
 *        when we are inspecting nested attributes.
 * @return Booolean true if the object was handled, false otherwise
 */
function wrappedPrimitivePreviewer(
  classObj,
  objectActor,
  grip,
  depth
) {
  const { safeRawObj } = objectActor;
  let v = null;
  try {
    v = classObj.prototype.valueOf.call(safeRawObj);
  } catch (ex) {
    // valueOf() can throw if the raw JS object is "misbehaved".
    return false;
  }

  if (v === null) {
    return false;
  }

  const canHandle = GenericObject(objectActor, grip, depth);
  if (!canHandle) {
    return false;
  }

  grip.preview.wrappedValue = objectActor.createValueGrip(
    ObjectUtils.makeDebuggeeValueIfNeeded(objectActor.obj, v),
    depth
  );
  return true;
}

/**
 * Previewer for Temporal objects
 *
 * @param cls
 *        The class of the object we're previewing (e.g. `Temporal.Instant`)
 * @param ObjectActor objectActor
 *        The object actor
 * @param Object grip
 *        The result grip to fill in
 */
function temporalPreviewer(cls, objectActor, grip) {
  grip.preview = {
    kind: "ObjectWithText",
    text: cls.prototype.toString.call(
      // In worker objectActor.safeRawObj is considered unsafe and is null
      objectActor.safeRawObj || objectActor.rawObj
    )
  }
}

/**
 * @param {ObjectActor} objectActor
 * @param {Object} grip: The grip built by the objectActor, for which we need to populate
 *                       the `preview` property.
 * @param {Number} depth
 *        Depth of the object compared to the top level object,
 *        when we are inspecting nested attributes.
 * @returns
 */
function GenericObject(objectActor, grip, depth) {
  const { obj, safeRawObj } = objectActor;
  if (grip.preview || grip.displayString || depth > 1) {
    return false;
  }

  const preview = (grip.preview = {
    kind: "Object",
    ownProperties: Object.create(null),
  });

  const names = ObjectUtils.getPropNamesFromObject(obj, safeRawObj);
  preview.ownPropertiesLength = names.length;

  let length,
    i = 0;
  let specialStringBehavior = objectActor.className === "String";
  if (specialStringBehavior) {
    length = DevToolsUtils.getProperty(obj, "length");
    if (typeof length != "number") {
      specialStringBehavior = false;
    }
  }

  for (const name of names) {
    if (specialStringBehavior && /^[0-9]+$/.test(name)) {
      const num = parseInt(name, 10);
      if (num.toString() === name && num >= 0 && num < length) {
        continue;
      }
    }

    const desc = propertyDescriptor(objectActor, name, depth, true);
    if (!desc) {
      continue;
    }

    preview.ownProperties[name] = desc;
    if (++i == OBJECT_PREVIEW_MAX_ITEMS) {
      break;
    }
  }

  if (i === OBJECT_PREVIEW_MAX_ITEMS) {
    return true;
  }

  const privatePropertiesSymbols = ObjectUtils.getSafePrivatePropertiesSymbols(
    obj
  );
  if (privatePropertiesSymbols.length > 0) {
    preview.privatePropertiesLength = privatePropertiesSymbols.length;
    preview.privateProperties = [];

    // Retrieve private properties, which are represented as non-enumerable Symbols
    for (const privateProperty of privatePropertiesSymbols) {
      if (
        !privateProperty.description ||
        !privateProperty.description.startsWith("#")
      ) {
        continue;
      }
      const descriptor = propertyDescriptor(objectActor, privateProperty, depth);
      if (!descriptor) {
        continue;
      }

      preview.privateProperties.push(
        Object.assign(
          {
            descriptor,
          },
          objectActor.createValueGrip(privateProperty, depth)
        )
      );

      if (++i == OBJECT_PREVIEW_MAX_ITEMS) {
        break;
      }
    }
  }

  if (i === OBJECT_PREVIEW_MAX_ITEMS) {
    return true;
  }

  const symbols = ObjectUtils.getSafeOwnPropertySymbols(obj);
  if (symbols.length > 0) {
    preview.ownSymbolsLength = symbols.length;
    preview.ownSymbols = [];

    for (const symbol of symbols) {
      const descriptor = propertyDescriptor(objectActor, symbol, depth, true);
      if (!descriptor) {
        continue;
      }

      preview.ownSymbols.push(
        Object.assign(
          {
            descriptor,
          },
          objectActor.createValueGrip(symbol, depth)
        )
      );

      if (++i == OBJECT_PREVIEW_MAX_ITEMS) {
        break;
      }
    }
  }

  if (i === OBJECT_PREVIEW_MAX_ITEMS) {
    return true;
  }

  const safeGetterValues = objectActor._findSafeGetterValues(
    Object.keys(preview.ownProperties),
    depth,
    OBJECT_PREVIEW_MAX_ITEMS - i
  );
  if (Object.keys(safeGetterValues).length) {
    preview.safeGetterValues = safeGetterValues;
  }

  return true;
}

// Preview functions that do not rely on the object class.
previewers.Object = [
  function TypedArray(objectActor, grip, depth) {
    const { obj, className } = objectActor;
    if (!ObjectUtils.isTypedArray(obj)) {
      return false;
    }

    grip.preview = {
      kind: "ArrayLike",
      length: ObjectUtils.getArrayLength(obj),
    };

    if (depth > 1) {
      return true;
    }

    const previewLength = Math.min(
      OBJECT_PREVIEW_MAX_ITEMS,
      grip.preview.length
    );
    grip.preview.items = [];
    const isBigIntArray = className.startsWith("BigInt") || className.startsWith("BigUint");

    for (let i = 0; i < previewLength; i++) {
      const desc = obj.getOwnPropertyDescriptor(i);
      if (!desc) {
        break;
      }

      // We need to create grips for items of BigInt arrays. Other typed arrays are fine
      // as they hold serializable primitives (Numbers)
      const item = isBigIntArray
        ? ObjectUtils.createBigIntValueGrip(desc.value)
        : desc.value;
      grip.preview.items.push(item);
    }

    return true;
  },

  function Error(objectActor, grip, depth) {
    if (!ERROR_CLASSNAMES.has(objectActor.className)) {
      return false;
    }

    const { obj, allowSideEffect = false } = objectActor;

    // The name and/or message could be getters, and even if it's unsafe,
    // we do want to show it to the user, unless the error is muted
    // (See Bug 1710694).
    const invokeUnsafeGetters = allowSideEffect && !obj.isMutedError;

    const name = DevToolsUtils.getProperty(obj, "name", invokeUnsafeGetters);
    const msg = DevToolsUtils.getProperty(obj, "message", invokeUnsafeGetters);
    const stack = DevToolsUtils.getProperty(obj, "stack");
    const fileName = DevToolsUtils.getProperty(obj, "fileName");
    const lineNumber = DevToolsUtils.getProperty(obj, "lineNumber");
    const columnNumber = DevToolsUtils.getProperty(obj, "columnNumber");

    grip.preview = {
      kind: "Error",
      name: objectActor.createValueGrip(name, depth),
      message: objectActor.createValueGrip(msg, depth),
      stack: objectActor.createValueGrip(stack, depth),
      fileName: objectActor.createValueGrip(fileName, depth),
      lineNumber: objectActor.createValueGrip(lineNumber, depth),
      columnNumber: objectActor.createValueGrip(columnNumber, depth),
    };

    const errorHasCause = obj.getOwnPropertyNames().includes("cause");
    if (errorHasCause) {
      grip.preview.cause = objectActor.createValueGrip(
        DevToolsUtils.getProperty(obj, "cause", true),
        depth
      );
    }

    return true;
  },

  function CSSMediaRule(objectActor, grip, depth) {
    const { safeRawObj } = objectActor;
    if (!safeRawObj || objectActor.className != "CSSMediaRule" || isWorker) {
      return false;
    }
    grip.preview = {
      kind: "ObjectWithText",
      text: objectActor.createValueGrip(safeRawObj.conditionText, depth),
    };
    return true;
  },

  function CSSStyleRule(objectActor, grip, depth) {
    const { safeRawObj } = objectActor;
    if (!safeRawObj || objectActor.className != "CSSStyleRule" || isWorker) {
      return false;
    }
    grip.preview = {
      kind: "ObjectWithText",
      text: objectActor.createValueGrip(safeRawObj.selectorText, depth),
    };
    return true;
  },

  function ObjectWithURL(objectActor, grip, depth) {
    const { safeRawObj } = objectActor;
    if (isWorker || !safeRawObj) {
      return false;
    }

    const isWindow = Window.isInstance(safeRawObj);
    if (!OBJECT_WITH_URL_CLASSNAMES.has(objectActor.className) && !isWindow) {
      return false;
    }

    let url;
    if (isWindow && safeRawObj.location) {
      try {
        url = safeRawObj.location.href;
      } catch(e) {
        // This can happen when we have a cross-process window.
        // In such case, let's retrieve the url from the iframe.
        // For window.top from a remote iframe, there's no way we can't retrieve the URL,
        // so return a label that help user know what's going on.
        url = safeRawObj.browsingContext?.embedderElement?.src || "Restricted";
      }
    } else if (safeRawObj.href) {
      url = safeRawObj.href;
    } else {
      return false;
    }

    grip.preview = {
      kind: "ObjectWithURL",
      url: objectActor.createValueGrip(url, depth),
    };

    return true;
  },

  function ArrayLike(objectActor, grip, depth) {
    const { safeRawObj } = objectActor;
    if (
      !safeRawObj ||
      !ARRAY_LIKE_CLASSNAMES.has(objectActor.className) ||
      typeof safeRawObj.length != "number" ||
      isWorker
    ) {
      return false;
    }

    grip.preview = {
      kind: "ArrayLike",
      length: safeRawObj.length,
    };

    if (depth > 1) {
      return true;
    }

    const items = (grip.preview.items = []);

    for (
      let i = 0;
      i < safeRawObj.length && items.length < OBJECT_PREVIEW_MAX_ITEMS;
      i++
    ) {
      const value = ObjectUtils.makeDebuggeeValueIfNeeded(objectActor.obj, safeRawObj[i]);
      items.push(objectActor.createValueGrip(value, depth));
    }

    return true;
  },

  function CSSStyleDeclaration(objectActor, grip, depth) {
    const { safeRawObj, className } = objectActor;
    if (
      !safeRawObj ||
      (className != "CSSStyleDeclaration" && className != "CSS2Properties") ||
      isWorker
    ) {
      return false;
    }

    grip.preview = {
      kind: "MapLike",
      size: safeRawObj.length,
    };

    const entries = (grip.preview.entries = []);

    for (let i = 0; i < OBJECT_PREVIEW_MAX_ITEMS && i < safeRawObj.length; i++) {
      const prop = safeRawObj[i];
      const value = safeRawObj.getPropertyValue(prop);
      entries.push([prop, objectActor.createValueGrip(value, depth)]);
    }

    return true;
  },

  function DOMNode(objectActor, grip, depth) {
    const { safeRawObj } = objectActor;
    if (
      objectActor.className == "Object" ||
      !safeRawObj ||
      !Node.isInstance(safeRawObj) ||
      isWorker
    ) {
      return false;
    }

    const { obj, className } = objectActor;

    const preview = (grip.preview = {
      kind: "DOMNode",
      nodeType: safeRawObj.nodeType,
      nodeName: safeRawObj.nodeName,
      isConnected: safeRawObj.isConnected === true,
    });

    if (safeRawObj.nodeType == safeRawObj.DOCUMENT_NODE && safeRawObj.location) {
      preview.location = objectActor.createValueGrip(safeRawObj.location.href, depth);
    } else if (className == "DocumentFragment") {
      preview.childNodesLength = safeRawObj.childNodes.length;

      if (depth < 2) {
        preview.childNodes = [];
        for (const node of safeRawObj.childNodes) {
          const actor = objectActor.createValueGrip(obj.makeDebuggeeValue(node), depth);
          preview.childNodes.push(actor);
          if (preview.childNodes.length == OBJECT_PREVIEW_MAX_ITEMS) {
            break;
          }
        }
      }
    } else if (Element.isInstance(safeRawObj)) {
      // For HTML elements (in an HTML document, at least), the nodeName is an
      // uppercased version of the actual element name.  Check for HTML
      // elements, that is elements in the HTML namespace, and lowercase the
      // nodeName in that case.
      if (safeRawObj.namespaceURI == "http://www.w3.org/1999/xhtml") {
        preview.nodeName = preview.nodeName.toLowerCase();
      }

      // Add preview for DOM element attributes.
      preview.attributes = {};
      preview.attributesLength = safeRawObj.attributes.length;
      for (const attr of safeRawObj.attributes) {
        preview.attributes[attr.nodeName] = objectActor.createValueGrip(attr.value, depth);
      }

      // Custom elements may have private properties. Ensure that we provide
      // enough information for ObjectInspector to know it should check for
      // them.
      const privatePropertiesSymbols = ObjectUtils.getSafePrivatePropertiesSymbols(
        obj
      );
      if (privatePropertiesSymbols.length > 0) {
        preview.privatePropertiesLength = privatePropertiesSymbols.length;
      }
    } else if (className == "Attr") {
      preview.value = objectActor.createValueGrip(safeRawObj.value, depth);
    } else if (
      className == "Text" ||
      className == "CDATASection" ||
      className == "Comment"
    ) {
      preview.textContent = objectActor.createValueGrip(safeRawObj.textContent, depth);
    }

    return true;
  },

  function DOMEvent(objectActor, grip, depth) {
    const { safeRawObj } = objectActor;
    if (!safeRawObj || !Event.isInstance(safeRawObj) || isWorker) {
      return false;
    }

    const { obj, className } = objectActor;
    const preview = (grip.preview = {
      kind: "DOMEvent",
      type: safeRawObj.type,
      properties: Object.create(null),
    });

    if (depth < 2) {
      const target = obj.makeDebuggeeValue(safeRawObj.target);
      preview.target = objectActor.createValueGrip(target, depth);
    }

    if (className == "KeyboardEvent") {
      preview.eventKind = "key";
      preview.modifiers = ObjectUtils.getModifiersForEvent(safeRawObj);
    }

    const props = ObjectUtils.getPropsForEvent(className);

    // Add event-specific properties.
    for (const prop of props) {
      let value = safeRawObj[prop];
      if (ObjectUtils.isObjectOrFunction(value)) {
        // Skip properties pointing to objects.
        if (depth > 1) {
          continue;
        }
        value = obj.makeDebuggeeValue(value);
      }
      preview.properties[prop] = objectActor.createValueGrip(value, depth);
    }

    // Add any properties we find on the event object.
    if (!props.length) {
      let i = 0;
      for (const prop in safeRawObj) {
        let value = safeRawObj[prop];
        if (
          prop == "target" ||
          prop == "type" ||
          value === null ||
          typeof value == "function"
        ) {
          continue;
        }
        if (value && typeof value == "object") {
          if (depth > 1) {
            continue;
          }
          value = obj.makeDebuggeeValue(value);
        }
        preview.properties[prop] = objectActor.createValueGrip(value, depth);
        if (++i == OBJECT_PREVIEW_MAX_ITEMS) {
          break;
        }
      }
    }

    return true;
  },

  function DOMException(objectActor, grip, depth) {
    const { safeRawObj } = objectActor;
    if (!safeRawObj || objectActor.className !== "DOMException" || isWorker) {
      return false;
    }

    grip.preview = {
      kind: "DOMException",
      name: objectActor.createValueGrip(safeRawObj.name, depth),
      message: objectActor.createValueGrip(safeRawObj.message, depth),
      code: objectActor.createValueGrip(safeRawObj.code, depth),
      result: objectActor.createValueGrip(safeRawObj.result, depth),
      filename: objectActor.createValueGrip(safeRawObj.filename, depth),
      lineNumber: objectActor.createValueGrip(safeRawObj.lineNumber, depth),
      columnNumber: objectActor.createValueGrip(safeRawObj.columnNumber, depth),
      stack: objectActor.createValueGrip(safeRawObj.stack, depth),
    };

    return true;
  },

  function Object(objectActor, grip, depth) {
    return GenericObject(objectActor, grip, depth);
  },
];

module.exports = previewers;
