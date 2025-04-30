/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* globals setImmediate, rpc */

"use strict";

/* General utilities used throughout devtools. */

var flags = require("resource://devtools/shared/flags.js");
var {
  getStack,
  callFunctionWithAsyncStack,
} = require("resource://devtools/shared/platform/stack.js");

const lazy = {};

if (!isWorker) {
  ChromeUtils.defineESModuleGetters(
    lazy,
    {
      DownloadPaths: "resource://gre/modules/DownloadPaths.sys.mjs",
      FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
      NetworkHelper:
        "resource://devtools/shared/network-observer/NetworkHelper.sys.mjs",
      ObjectUtils: "resource://gre/modules/ObjectUtils.sys.mjs",
    },
    { global: "contextual" }
  );
}

// Native getters which are considered to be side effect free.
ChromeUtils.defineLazyGetter(lazy, "sideEffectFreeGetters", () => {
  const {
    getters,
  } = require("resource://devtools/server/actors/webconsole/eager-ecma-allowlist.js");

  const map = new Map();
  for (const n of getters) {
    if (!map.has(n.name)) {
      map.set(n.name, []);
    }
    map.get(n.name).push(n);
  }

  return map;
});

// Using this name lets the eslint plugin know about lazy defines in
// this file.
var DevToolsUtils = exports;

// Re-export the thread-safe utils.
const ThreadSafeDevToolsUtils = require("resource://devtools/shared/ThreadSafeDevToolsUtils.js");
for (const key of Object.keys(ThreadSafeDevToolsUtils)) {
  exports[key] = ThreadSafeDevToolsUtils[key];
}

/**
 * Waits for the next tick in the event loop to execute a callback.
 */
exports.executeSoon = function (fn) {
  if (isWorker) {
    setImmediate(fn);
  } else {
    let executor;
    // Only enable async stack reporting when DEBUG_JS_MODULES is set
    // (customized local builds) to avoid a performance penalty.
    if (AppConstants.DEBUG_JS_MODULES || flags.testing) {
      const stack = getStack();
      executor = () => {
        callFunctionWithAsyncStack(fn, stack, "DevToolsUtils.executeSoon");
      };
    } else {
      executor = fn;
    }
    Services.tm.dispatchToMainThread({
      run: exports.makeInfallible(executor),
    });
  }
};

/**
 * Similar to executeSoon, but enters microtask before executing the callback
 * if this is called on the main thread.
 */
exports.executeSoonWithMicroTask = function (fn) {
  if (isWorker) {
    setImmediate(fn);
  } else {
    let executor;
    // Only enable async stack reporting when DEBUG_JS_MODULES is set
    // (customized local builds) to avoid a performance penalty.
    if (AppConstants.DEBUG_JS_MODULES || flags.testing) {
      const stack = getStack();
      executor = () => {
        callFunctionWithAsyncStack(
          fn,
          stack,
          "DevToolsUtils.executeSoonWithMicroTask"
        );
      };
    } else {
      executor = fn;
    }
    Services.tm.dispatchToMainThreadWithMicroTask({
      run: exports.makeInfallible(executor),
    });
  }
};

/**
 * Waits for the next tick in the event loop.
 *
 * @return Promise
 *         A promise that is resolved after the next tick in the event loop.
 */
exports.waitForTick = function () {
  return new Promise(resolve => {
    exports.executeSoon(resolve);
  });
};

/**
 * Waits for the specified amount of time to pass.
 *
 * @param number delay
 *        The amount of time to wait, in milliseconds.
 * @return Promise
 *         A promise that is resolved after the specified amount of time passes.
 */
exports.waitForTime = function (delay) {
  return new Promise(resolve => setTimeout(resolve, delay));
};

/**
 * Like ChromeUtils.defineLazyGetter, but with a |this| sensitive getter that
 * allows the lazy getter to be defined on a prototype and work correctly with
 * instances.
 *
 * @param Object object
 *        The prototype object to define the lazy getter on.
 * @param String key
 *        The key to define the lazy getter on.
 * @param Function callback
 *        The callback that will be called to determine the value. Will be
 *        called with the |this| value of the current instance.
 */
exports.defineLazyPrototypeGetter = function (object, key, callback) {
  Object.defineProperty(object, key, {
    configurable: true,
    get() {
      const value = callback.call(this);

      Object.defineProperty(this, key, {
        configurable: true,
        writable: true,
        value,
      });

      return value;
    },
  });
};

/**
 * Safely get the property value from a Debugger.Object for a given key. Walks
 * the prototype chain until the property is found.
 *
 * @param {Debugger.Object} object
 *        The Debugger.Object to get the value from.
 * @param {String} key
 *        The key to look for.
 * @param {Boolean} invokeUnsafeGetter (defaults to false).
 *        Optional boolean to indicate if the function should execute unsafe getter
 *        in order to retrieve its result's properties.
 *        ⚠️ This should be set to true *ONLY* on user action as it may cause side-effects
 *        in the content page ⚠️
 * @return Any
 */
exports.getProperty = function (object, key, invokeUnsafeGetters = false) {
  const root = object;
  while (object && exports.isSafeDebuggerObject(object)) {
    let desc;
    try {
      desc = object.getOwnPropertyDescriptor(key);
    } catch (e) {
      // The above can throw when the debuggee does not subsume the object's
      // compartment, or for some WrappedNatives like Cu.Sandbox.
      return undefined;
    }
    if (desc) {
      if ("value" in desc) {
        return desc.value;
      }
      // Call the getter if it's safe.
      if (exports.hasSafeGetter(desc) || invokeUnsafeGetters === true) {
        try {
          return desc.get.call(root).return;
        } catch (e) {
          // If anything goes wrong report the error and return undefined.
          exports.reportException("getProperty", e);
        }
      }
      return undefined;
    }
    object = object.proto;
  }
  return undefined;
};

/**
 * Removes all the non-opaque security wrappers of a debuggee object.
 *
 * @param obj Debugger.Object
 *        The debuggee object to be unwrapped.
 * @return Debugger.Object|null|undefined
 *      - If the object has no wrapper, the same `obj` is returned. Note DeadObject
 *        objects belong to this case.
 *      - Otherwise, if the debuggee doesn't subsume object's compartment, returns `null`.
 *      - Otherwise, if the object belongs to an invisible-to-debugger compartment,
 *        returns `undefined`.
 *      - Otherwise, returns the unwrapped object.
 */
exports.unwrap = function unwrap(obj) {
  // Check if `obj` has an opaque wrapper.
  if (obj.class === "Opaque") {
    return obj;
  }

  // Attempt to unwrap via `obj.unwrap()`. Note that:
  // - This will return `null` if the debuggee does not subsume object's compartment.
  // - This will throw if the object belongs to an invisible-to-debugger compartment.
  // - This will return `obj` if there is no wrapper.
  let unwrapped;
  try {
    unwrapped = obj.unwrap();
  } catch (err) {
    return undefined;
  }

  // Check if further unwrapping is not possible.
  if (!unwrapped || unwrapped === obj) {
    return unwrapped;
  }

  // Recursively remove additional security wrappers.
  return unwrap(unwrapped);
};

/**
 * Checks whether a debuggee object is safe. Unsafe objects may run proxy traps or throw
 * when using `proto`, `isExtensible`, `isFrozen` or `isSealed`. Note that safe objects
 * may still throw when calling `getOwnPropertyNames`, `getOwnPropertyDescriptor`, etc.
 * Also note DeadObject objects are considered safe.
 *
 * @param obj Debugger.Object
 *        The debuggee object to be checked.
 * @return boolean
 */
exports.isSafeDebuggerObject = function (obj) {
  const unwrapped = exports.unwrap(obj);

  // Objects belonging to an invisible-to-debugger compartment might be proxies,
  // so just in case consider them unsafe.
  if (unwrapped === undefined) {
    return false;
  }

  // If the debuggee does not subsume the object's compartment, most properties won't
  // be accessible. Cross-origin Window and Location objects might expose some, though.
  // Therefore, it must be considered safe. Note that proxy objects have fully opaque
  // security wrappers, so proxy traps won't run in this case.
  if (unwrapped === null) {
    return true;
  }

  // Proxy objects can run traps when accessed. `isProxy` getter is called on `unwrapped`
  // instead of on `obj` in order to detect proxies behind transparent wrappers.
  if (unwrapped.isProxy) {
    return false;
  }

  return true;
};

/**
 * Determines if a descriptor has a getter which doesn't call into JavaScript.
 *
 * @param Object desc
 *        The descriptor to check for a safe getter.
 * @return Boolean
 *         Whether a safe getter was found.
 */
exports.hasSafeGetter = function (desc) {
  // Scripted functions that are CCWs will not appear scripted until after
  // unwrapping.
  let fn = desc.get;
  fn = fn && exports.unwrap(fn);
  if (!fn) {
    return false;
  }
  if (!fn.callable || fn.class !== "Function") {
    return false;
  }
  if (fn.script !== undefined) {
    // This is scripted function.
    return false;
  }

  // This is a getter with native function.

  // We assume all DOM getters have no major side effect, and they are
  // eagerly-evaluateable.
  //
  // JitInfo is used only by methods/accessors in WebIDL, and being
  // "a getter with JitInfo" can be used as a condition to check if given
  // function is DOM getter.
  //
  // This includes privileged interfaces in addition to standard web APIs.
  if (fn.isNativeGetterWithJitInfo()) {
    return true;
  }

  // Apply explicit allowlist.
  const natives = lazy.sideEffectFreeGetters.get(fn.name);
  return natives && natives.some(n => fn.isSameNative(n));
};

/**
 * Check that the property value from a Debugger.Object for a given key is an unsafe
 * getter or not. Walks the prototype chain until the property is found.
 *
 * @param {Debugger.Object} object
 *        The Debugger.Object to check on.
 * @param {String} key
 *        The key to look for.
 * @param {Boolean} invokeUnsafeGetter (defaults to false).
 *        Optional boolean to indicate if the function should execute unsafe getter
 *        in order to retrieve its result's properties.
 * @return Boolean
 */
exports.isUnsafeGetter = function (object, key) {
  while (object && exports.isSafeDebuggerObject(object)) {
    let desc;
    try {
      desc = object.getOwnPropertyDescriptor(key);
    } catch (e) {
      // The above can throw when the debuggee does not subsume the object's
      // compartment, or for some WrappedNatives like Cu.Sandbox.
      return false;
    }
    if (desc) {
      if (Object.getOwnPropertyNames(desc).includes("get")) {
        return !exports.hasSafeGetter(desc);
      }
    }
    object = object.proto;
  }

  return false;
};

/**
 * Check if it is safe to read properties and execute methods from the given JS
 * object. Safety is defined as being protected from unintended code execution
 * from content scripts (or cross-compartment code).
 *
 * See bugs 945920 and 946752 for discussion.
 *
 * @type Object obj
 *       The object to check.
 * @return Boolean
 *         True if it is safe to read properties from obj, or false otherwise.
 */
exports.isSafeJSObject = function (obj) {
  // If we are running on a worker thread, Cu is not available. In this case,
  // we always return false, just to be on the safe side.
  if (isWorker) {
    return false;
  }

  if (
    Cu.getGlobalForObject(obj) == Cu.getGlobalForObject(exports.isSafeJSObject)
  ) {
    // obj is not a cross-compartment wrapper.
    return true;
  }

  // Xray wrappers protect against unintended code execution.
  if (Cu.isXrayWrapper(obj)) {
    return true;
  }

  // If there aren't Xrays, only allow chrome objects.
  const principal = Cu.getObjectPrincipal(obj);
  if (!principal.isSystemPrincipal) {
    return false;
  }

  // Scripted proxy objects without Xrays can run their proxy traps.
  if (Cu.isProxy(obj)) {
    return false;
  }

  // Even if `obj` looks safe, an unsafe object in its prototype chain may still
  // run unintended code, e.g. when using the `instanceof` operator.
  const proto = Object.getPrototypeOf(obj);
  if (proto && !exports.isSafeJSObject(proto)) {
    return false;
  }

  // Allow non-problematic chrome objects.
  return true;
};

/**
 * Dump with newline - This is a logging function that will only output when
 * the preference "devtools.debugger.log" is set to true. Typically it is used
 * for logging the remote debugging protocol calls.
 */
exports.dumpn = function (str) {
  if (flags.wantLogging) {
    dump("DBG-SERVER: " + str + "\n");
  }
};

/**
 * Dump verbose - This is a verbose logger for low-level tracing, that is typically
 * used to provide information about the remote debugging protocol's transport
 * mechanisms. The logging can be enabled by changing the preferences
 * "devtools.debugger.log" and "devtools.debugger.log.verbose" to true.
 */
exports.dumpv = function (msg) {
  if (flags.wantVerbose) {
    exports.dumpn(msg);
  }
};

/**
 * Defines a getter on a specified object that will be created upon first use.
 *
 * @param object
 *        The object to define the lazy getter on.
 * @param name
 *        The name of the getter to define on object.
 * @param lambda
 *        A function that returns what the getter should return.  This will
 *        only ever be called once.
 */
exports.defineLazyGetter = function (object, name, lambda) {
  Object.defineProperty(object, name, {
    get() {
      delete object[name];
      object[name] = lambda.apply(object);
      return object[name];
    },
    configurable: true,
    enumerable: true,
  });
};

DevToolsUtils.defineLazyGetter(this, "AppConstants", () => {
  if (isWorker) {
    return {};
  }
  return ChromeUtils.importESModule(
    "resource://gre/modules/AppConstants.sys.mjs",
    { global: "contextual" }
  ).AppConstants;
});

/**
 * No operation. The empty function.
 */
exports.noop = function () {};

let assertionFailureCount = 0;

Object.defineProperty(exports, "assertionFailureCount", {
  get() {
    return assertionFailureCount;
  },
});

function reallyAssert(condition, message) {
  if (!condition) {
    assertionFailureCount++;
    const err = new Error("Assertion failure: " + message);
    exports.reportException("DevToolsUtils.assert", err);
    throw err;
  }
}

/**
 * DevToolsUtils.assert(condition, message)
 *
 * @param Boolean condition
 * @param String message
 *
 * Assertions are enabled when any of the following are true:
 *   - This is a DEBUG_JS_MODULES build
 *   - flags.testing is set to true
 *
 * If assertions are enabled, then `condition` is checked and if false-y, the
 * assertion failure is logged and then an error is thrown.
 *
 * If assertions are not enabled, then this function is a no-op.
 */
Object.defineProperty(exports, "assert", {
  get: () =>
    AppConstants.DEBUG_JS_MODULES || flags.testing
      ? reallyAssert
      : exports.noop,
});

DevToolsUtils.defineLazyGetter(this, "NetUtil", () => {
  return ChromeUtils.importESModule("resource://gre/modules/NetUtil.sys.mjs", {
    global: "contextual",
  }).NetUtil;
});

/**
 * Performs a request to load the desired URL and returns a promise.
 *
 * @param urlIn String
 *        The URL we will request.
 * @param aOptions Object
 *        An object with the following optional properties:
 *        - loadFromCache: if false, will bypass the cache and
 *          always load fresh from the network (default: true)
 *        - policy: the nsIContentPolicy type to apply when fetching the URL
 *                  (only works when loading from system principal)
 *        - window: the window to get the loadGroup from
 *        - charset: the charset to use if the channel doesn't provide one
 *        - principal: the principal to use, if omitted, the request is loaded
 *                     with a content principal corresponding to the url being
 *                     loaded, using the origin attributes of the window, if any.
 *        - headers: extra headers
 *        - cacheKey: when loading from cache, use this key to retrieve a cache
 *                    specific to a given SHEntry. (Allows loading POST
 *                    requests from cache)
 * @returns Promise that resolves with an object with the following members on
 *          success:
 *           - content: the document at that URL, as a string,
 *           - contentType: the content type of the document
 *
 *          If an error occurs, the promise is rejected with that error.
 *
 * XXX: It may be better to use nsITraceableChannel to get to the sources
 * without relying on caching when we can (not for eval, etc.):
 * http://www.softwareishard.com/blog/firebug/nsitraceablechannel-intercept-http-traffic/
 */
function mainThreadFetch(
  urlIn,
  aOptions = {
    loadFromCache: true,
    policy: Ci.nsIContentPolicy.TYPE_OTHER,
    window: null,
    charset: null,
    principal: null,
    headers: null,
    cacheKey: 0,
  }
) {
  return new Promise((resolve, reject) => {
    // Create a channel.
    const url = urlIn.split(" -> ").pop();
    let channel;
    try {
      channel = newChannelForURL(url, aOptions);
    } catch (ex) {
      reject(ex);
      return;
    }

    channel.loadInfo.isInDevToolsContext = true;

    // Set the channel options.
    channel.loadFlags = aOptions.loadFromCache
      ? channel.LOAD_FROM_CACHE
      : channel.LOAD_BYPASS_CACHE;

    if (aOptions.loadFromCache && channel instanceof Ci.nsICacheInfoChannel) {
      // If DevTools intents to load the content from the cache,
      // we make the LOAD_FROM_CACHE flag preferred over LOAD_BYPASS_CACHE.
      channel.preferCacheLoadOverBypass = true;

      // When loading from cache, the cacheKey allows us to target a specific
      // SHEntry and offer ways to restore POST requests from cache.
      if (aOptions.cacheKey != 0) {
        channel.cacheKey = aOptions.cacheKey;
      }
    }

    if (aOptions.headers && channel instanceof Ci.nsIHttpChannel) {
      for (const h in aOptions.headers) {
        channel.setRequestHeader(h, aOptions.headers[h], /* aMerge = */ false);
      }
    }

    if (aOptions.window) {
      // Respect private browsing.
      channel.loadGroup = aOptions.window.docShell.QueryInterface(
        Ci.nsIDocumentLoader
      ).loadGroup;
    }

    // eslint-disable-next-line complexity
    const onResponse = (stream, status, request) => {
      if (!Components.isSuccessCode(status)) {
        reject(new Error(`Failed to fetch ${url}. Code ${status}.`));
        return;
      }

      try {
        // We cannot use NetUtil to do the charset conversion as if charset
        // information is not available and our default guess is wrong the method
        // might fail and we lose the stream data. This means we can't fall back
        // to using the locale default encoding (bug 1181345).

        // Read and decode the data according to the locale default encoding.

        let available;
        try {
          available = stream.available();
        } catch (ex) {
          if (ex.name === "NS_BASE_STREAM_CLOSED") {
            // Empty files cause NS_BASE_STREAM_CLOSED exception.
            // If there was a real stream error, we would have already rejected above.
            resolve({
              content: "",
              contentType: "text/plain",
            });
            return;
          }

          reject(ex);
        }
        let source = NetUtil.readInputStreamToString(stream, available);
        stream.close();

        // We do our own BOM sniffing here because there's no convenient
        // implementation of the "decode" algorithm
        // (https://encoding.spec.whatwg.org/#decode) exposed to JS.
        let bomCharset = null;
        if (
          available >= 3 &&
          source.codePointAt(0) == 0xef &&
          source.codePointAt(1) == 0xbb &&
          source.codePointAt(2) == 0xbf
        ) {
          bomCharset = "UTF-8";
          source = source.slice(3);
        } else if (
          available >= 2 &&
          source.codePointAt(0) == 0xfe &&
          source.codePointAt(1) == 0xff
        ) {
          bomCharset = "UTF-16BE";
          source = source.slice(2);
        } else if (
          available >= 2 &&
          source.codePointAt(0) == 0xff &&
          source.codePointAt(1) == 0xfe
        ) {
          bomCharset = "UTF-16LE";
          source = source.slice(2);
        }

        // If the channel or the caller has correct charset information, the
        // content will be decoded correctly. If we have to fall back to UTF-8 and
        // the guess is wrong, the conversion fails and convertToUnicode returns
        // the input unmodified. Essentially we try to decode the data as UTF-8
        // and if that fails, we use the locale specific default encoding. This is
        // the best we can do if the source does not provide charset info.
        let charset = bomCharset;
        if (!charset) {
          try {
            charset = channel.contentCharset;
          } catch (e) {
            // Accessing `contentCharset` on content served by a service worker in
            // non-e10s may throw.
          }
        }
        if (!charset) {
          charset = aOptions.charset || "UTF-8";
        }
        const unicodeSource = lazy.NetworkHelper.convertToUnicode(
          source,
          charset
        );

        // Look for any source map URL in the response.
        let sourceMapURL;
        if (request instanceof Ci.nsIHttpChannel) {
          try {
            sourceMapURL = request.getResponseHeader("SourceMap");
          } catch (e) {}
          if (!sourceMapURL) {
            try {
              sourceMapURL = request.getResponseHeader("X-SourceMap");
            } catch (e) {}
          }
        }

        resolve({
          content: unicodeSource,
          contentType: request.contentType,
          sourceMapURL,
        });
      } catch (ex) {
        reject(ex);
      }
    };

    // Open the channel
    try {
      NetUtil.asyncFetch(channel, onResponse);
    } catch (ex) {
      reject(ex);
    }
  });
}

/**
 * Opens a channel for given URL. Tries a bit harder than NetUtil.newChannel.
 *
 * @param {String} url - The URL to open a channel for.
 * @param {Object} options - The options object passed to @method fetch.
 * @return {nsIChannel} - The newly created channel. Throws on failure.
 */
function newChannelForURL(url, { policy, window, principal }) {
  const securityFlags =
    Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL;

  let uri;
  try {
    uri = Services.io.newURI(url);
  } catch (e) {
    // In the xpcshell tests, the script url is the absolute path of the test
    // file, which will make a malformed URI error be thrown. Add the file
    // scheme to see if it helps.
    uri = Services.io.newURI("file://" + url);
  }

  // In xpcshell tests on Windows, opening the channel
  // can throw NS_ERROR_UNKNOWN_PROTOCOL if the external protocol isn't
  // supported by Windows, so we also need to handle that case here if
  // parsing the URL above doesn't throw.
  const handler = Services.io.getProtocolHandler(uri.scheme);
  if (
    handler instanceof Ci.nsIExternalProtocolHandler &&
    !handler.externalAppExistsForScheme(uri.scheme)
  ) {
    uri = Services.io.newURI("file://" + url);
  }

  const channelOptions = {
    contentPolicyType: policy,
    securityFlags,
    uri,
  };

  // Ensure that we have some contentPolicyType type set if one was
  // not provided.
  if (!channelOptions.contentPolicyType) {
    channelOptions.contentPolicyType = Ci.nsIContentPolicy.TYPE_OTHER;
  }

  // If a window is provided, always use it's document as the loadingNode.
  // This will provide the correct principal, origin attributes, service
  // worker controller, etc.
  if (window) {
    channelOptions.loadingNode = window.document;
  } else {
    // If a window is not provided, then we must set a loading principal.

    // If the caller did not provide a principal, then we use the URI
    // to create one.  Note, it's not clear what use cases require this
    // and it may not be correct.
    let prin = principal;
    if (!prin) {
      prin = Services.scriptSecurityManager.createContentPrincipal(uri, {});
    }

    channelOptions.loadingPrincipal = prin;
  }

  return NetUtil.newChannel(channelOptions);
}

// Fetch is defined differently depending on whether we are on the main thread
// or a worker thread.
if (this.isWorker) {
  // Services is not available in worker threads, nor is there any other way
  // to fetch a URL. We need to enlist the help from the main thread here, by
  // issuing an rpc request, to fetch the URL on our behalf.
  exports.fetch = function (url, options) {
    return rpc("fetch", url, options);
  };
} else {
  exports.fetch = mainThreadFetch;
}

/**
 * Open the file at the given path for reading.
 *
 * @param {String} filePath
 *
 * @returns Promise<nsIInputStream>
 */
exports.openFileStream = function (filePath) {
  return new Promise((resolve, reject) => {
    const uri = NetUtil.newURI(new lazy.FileUtils.File(filePath));
    NetUtil.asyncFetch(
      { uri, loadUsingSystemPrincipal: true },
      (stream, result) => {
        if (!Components.isSuccessCode(result)) {
          reject(new Error(`Could not open "${filePath}": result = ${result}`));
          return;
        }

        resolve(stream);
      }
    );
  });
};

/**
 * Save the given data to disk after asking the user where to do so.
 *
 * @param {Window} parentWindow
 *        The parent window to use to display the filepicker.
 * @param {UInt8Array} dataArray
 *        The data to write to the file.
 * @param {String} fileName
 *        The suggested filename.
 * @param {Array} filters
 *        An array of object of the following shape:
 *          - pattern: A pattern for accepted files (example: "*.js")
 *          - label: The label that will be displayed in the save file dialog.
 * @return {String|null}
 *        The path to the local saved file, if saved.
 */
exports.saveAs = async function (
  parentWindow,
  dataArray,
  fileName = "",
  filters = []
) {
  let returnFile;
  try {
    returnFile = await exports.showSaveFileDialog(
      parentWindow,
      lazy.DownloadPaths.sanitize(fileName),
      filters
    );
  } catch (ex) {
    return null;
  }

  // Sanitize the filename again in case the user renamed the file to use a
  // vulnerable extension.
  returnFile.leafName = lazy.DownloadPaths.sanitize(returnFile.leafName);

  await IOUtils.write(returnFile.path, dataArray, {
    tmpPath: returnFile.path + ".tmp",
  });

  return returnFile.path;
};

/**
 * Show file picker and return the file user selected.
 *
 * @param {nsIWindow} parentWindow
 *        Optional parent window. If null the parent window of the file picker
 *        will be the window of the attached input element.
 * @param {String} suggestedFilename
 *        The suggested filename.
 * @param {Array} filters
 *        An array of object of the following shape:
 *          - pattern: A pattern for accepted files (example: "*.js")
 *          - label: The label that will be displayed in the save file dialog.
 * @return {Promise}
 *         A promise that is resolved after the file is selected by the file picker
 */
exports.showSaveFileDialog = function (
  parentWindow,
  suggestedFilename,
  filters = []
) {
  const fp = Cc["@mozilla.org/filepicker;1"].createInstance(Ci.nsIFilePicker);

  if (suggestedFilename) {
    fp.defaultString = suggestedFilename;
  }

  fp.init(parentWindow.browsingContext, null, fp.modeSave);
  if (Array.isArray(filters) && filters.length) {
    for (const { pattern, label } of filters) {
      fp.appendFilter(label, pattern);
    }
  } else {
    fp.appendFilters(fp.filterAll);
  }

  return new Promise((resolve, reject) => {
    fp.open(result => {
      if (result == Ci.nsIFilePicker.returnCancel) {
        reject();
      } else {
        resolve(fp.file);
      }
    });
  });
};

/*
 * All of the flags have been moved to a different module. Make sure
 * nobody is accessing them anymore, and don't write new code using
 * them. We can remove this code after a while.
 */
function errorOnFlag(exports, name) {
  Object.defineProperty(exports, name, {
    get: () => {
      const msg =
        `Cannot get the flag ${name}. ` +
        `Use the "devtools/shared/flags" module instead`;
      console.error(msg);
      throw new Error(msg);
    },
    set: () => {
      const msg =
        `Cannot set the flag ${name}. ` +
        `Use the "devtools/shared/flags" module instead`;
      console.error(msg);
      throw new Error(msg);
    },
  });
}

errorOnFlag(exports, "testing");
errorOnFlag(exports, "wantLogging");
errorOnFlag(exports, "wantVerbose");

// Calls the property with the given `name` on the given `object`, where
// `name` is a string, and `object` a Debugger.Object instance.
//
// This function uses only the Debugger.Object API to call the property. It
// avoids the use of unsafeDeference. This is useful for example in workers,
// where unsafeDereference will return an opaque security wrapper to the
// referent.
function callPropertyOnObject(object, name, ...args) {
  // Find the property.
  let descriptor;
  let proto = object;
  do {
    descriptor = proto.getOwnPropertyDescriptor(name);
    if (descriptor !== undefined) {
      break;
    }
    proto = proto.proto;
  } while (proto !== null);
  if (descriptor === undefined) {
    throw new Error("No such property");
  }
  const value = descriptor.value;
  if (typeof value !== "object" || value === null || !("callable" in value)) {
    throw new Error("Not a callable object.");
  }

  if (value.script !== undefined) {
    throw new Error(
      "The property isn't a native function and will execute code in the debuggee"
    );
  }

  // Call the property.
  const result = value.call(object, ...args);
  if (result === null) {
    throw new Error("Code was terminated.");
  }
  if ("throw" in result) {
    throw result.throw;
  }
  return result.return;
}

exports.callPropertyOnObject = callPropertyOnObject;

// Convert a Debugger.Object wrapping an iterator into an iterator in the
// debugger's realm.
function* makeDebuggeeIterator(object) {
  while (true) {
    const nextValue = callPropertyOnObject(object, "next");
    if (exports.getProperty(nextValue, "done")) {
      break;
    }
    yield exports.getProperty(nextValue, "value");
  }
}

exports.makeDebuggeeIterator = makeDebuggeeIterator;

/**
 * Shared helper to retrieve the topmost window. This can be used to retrieve the chrome
 * window embedding the DevTools frame.
 */
function getTopWindow(win) {
  return win.windowRoot ? win.windowRoot.ownerGlobal : win.top;
}

exports.getTopWindow = getTopWindow;

/**
 * Check whether two objects are identical by performing
 * a deep equality check on their properties and values.
 * See toolkit/modules/ObjectUtils.jsm for implementation.
 *
 * @param {Object} a
 * @param {Object} b
 * @return {Boolean}
 */
exports.deepEqual = (a, b) => {
  return lazy.ObjectUtils.deepEqual(a, b);
};

function isWorkerDebuggerAlive(dbg) {
  // Some workers are zombies. `isClosed` is false, but nothing works.
  // `postMessage` is a noop, `addListener`'s `onClosed` doesn't work.
  // (Ignore dbg without `window` as they aren't related to docShell
  //  and probably do not suffer form this issue)
  return !dbg.isClosed && (!dbg.window || dbg.window.docShell);
}
exports.isWorkerDebuggerAlive = isWorkerDebuggerAlive;
