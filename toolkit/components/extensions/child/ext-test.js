/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

ChromeUtils.defineLazyGetter(this, "isXpcshell", function () {
  return Services.env.exists("XPCSHELL_TEST_PROFILE_DIR");
});

/**
 * Checks whether the given error matches the given expectations.
 *
 * @param {*} error
 *        The error to check.
 * @param {string | RegExp | Function | null} expectedError
 *        The expectation to check against. If this parameter is:
 *
 *        - a string, the error message must exactly equal the string.
 *        - a regular expression, it must match the error message.
 *        - a function, it is called with the error object and its
 *          return value is returned.
 * @param {BaseContext} context
 *
 * @returns {boolean}
 *        True if the error matches the expected error.
 */
const errorMatches = (error, expectedError, context) => {
  if (
    typeof error === "object" &&
    error !== null &&
    !context.principal.subsumes(Cu.getObjectPrincipal(error))
  ) {
    Cu.reportError("Error object belongs to the wrong scope.");
    return false;
  }

  if (typeof expectedError === "function") {
    return context.runSafeWithoutClone(expectedError, error);
  }

  if (
    typeof error !== "object" ||
    error == null ||
    typeof error.message !== "string"
  ) {
    return false;
  }

  if (typeof expectedError === "string") {
    return error.message === expectedError;
  }

  try {
    return expectedError.test(error.message);
  } catch (e) {
    Cu.reportError(e);
  }

  return false;
};

// Checks whether |v| should use string serialization instead of JSON.
function useStringInsteadOfJSON(v) {
  return (
    // undefined to string, or else it is omitted from object after stringify.
    v === undefined ||
    // Values that would have become null.
    (typeof v === "number" && (isNaN(v) || !isFinite(v)))
  );
}

// A very strict deep equality comparator that throws for unsupported values.
// For context, see https://bugzilla.mozilla.org/show_bug.cgi?id=1782816#c2
function deepEquals(a, b) {
  // Some values don't have a JSON representation. To disambiguate from null or
  // regular strings, we prepend this prefix instead.
  const NON_JSON_PREFIX = "#NOT_JSON_SERIALIZABLE#";

  function replacer(key, value) {
    if (typeof value == "object" && value !== null && !Array.isArray(value)) {
      const cls = ChromeUtils.getClassName(value);
      if (cls === "Object") {
        // Return plain object with keys sorted in a predictable order.
        return Object.fromEntries(
          Object.keys(value)
            .sort()
            .map(k => [k, value[k]])
        );
      }
      // Just throw to avoid potentially inaccurate serializations (e.g. {}).
      throw new ExtensionUtils.ExtensionError(`Unsupported obj type: ${cls}`);
    }

    if (useStringInsteadOfJSON(value)) {
      return `${NON_JSON_PREFIX}${value}`;
    }
    return value;
  }
  return JSON.stringify(a, replacer) === JSON.stringify(b, replacer);
}

/**
 * Serializes the given value for use in informative assertion messages.
 *
 * @param {*} value
 * @returns {string}
 */
const toSource = value => {
  function cannotJSONserialize(v) {
    return (
      useStringInsteadOfJSON(v) ||
      // Not a plain object. E.g. [object X], /regexp/, etc.
      (typeof v == "object" &&
        v !== null &&
        !Array.isArray(v) &&
        ChromeUtils.getClassName(v) !== "Object")
    );
  }
  try {
    if (cannotJSONserialize(value)) {
      return String(value);
    }

    const replacer = (k, v) => (cannotJSONserialize(v) ? String(v) : v);
    return JSON.stringify(value, replacer);
  } catch (e) {
    return "<unknown>";
  }
};

this.test = class extends ExtensionAPI {
  getAPI(context) {
    const CONTEXT_DESTROYED = "Test context destroyed.";
    const { extension } = context;
    let running = false;
    let testTasks = [];
    let unnamed = 0;

    async function runTasks(tests) {
      testTasks.push(...tests);

      // If still running tasks from a previous call, queue new ones and bail.
      if (running) {
        return;
      }

      let onClosed = Promise.withResolvers();
      onClosed.close = () => onClosed.reject(CONTEXT_DESTROYED);
      context.callOnClose(onClosed);

      try {
        running = true;
        while (testTasks.length) {
          let task = testTasks.shift();
          let name = task.name || `unnamed_test_${++unnamed}`;
          let stack = getStack(context.getCaller());
          extension.emit("test-task-start", name, stack);
          try {
            await Promise.race([task(), onClosed.promise]);

            if (!context.active) {
              assertTrue(false, CONTEXT_DESTROYED);
              throw new ExtensionUtils.ExtensionError(CONTEXT_DESTROYED);
            }
          } catch (e) {
            let err = `Exception running ${name}: ${e.message}`;
            assertTrue(false, err);
            Cu.reportError(err);
            throw e;
          } finally {
            extension.emit("test-task-done", testTasks.length, name, stack);
          }
        }
      } finally {
        context.forgetOnClose(onClosed);
        testTasks.length = 0;
        running = false;
      }
    }

    function getStack(savedFrame = null) {
      if (savedFrame) {
        return ChromeUtils.createError("", savedFrame).stack.replace(
          /^/gm,
          "    "
        );
      }
      return new context.Error().stack.replace(/^/gm, "    ");
    }

    function assertTrue(value, msg) {
      extension.emit(
        "test-result",
        Boolean(value),
        String(msg),
        getStack(context.getCaller())
      );
    }

    class TestEventManager extends EventManager {
      constructor(...args) {
        super(...args);

        // A map to keep track of the listeners wrappers being added in
        // addListener (the wrapper will be needed to be able to remove
        // the listener from this EventManager instance if the extension
        // does call test.onMessage.removeListener).
        this._listenerWrappers = new Map();
        context.callOnClose({
          close: () => this._listenerWrappers.clear(),
        });
      }

      addListener(callback, ...args) {
        const listenerWrapper = function (...args) {
          try {
            callback.call(this, ...args);
          } catch (e) {
            assertTrue(false, `${e}\n${e.stack}`);
          }
        };
        super.addListener(listenerWrapper, ...args);
        this._listenerWrappers.set(callback, listenerWrapper);
      }

      removeListener(callback) {
        if (!this._listenerWrappers.has(callback)) {
          return;
        }

        super.removeListener(this._listenerWrappers.get(callback));
        this._listenerWrappers.delete(callback);
      }
    }

    if (!Cu.isInAutomation && !isXpcshell) {
      return { test: {} };
    }

    return {
      test: {
        withHandlingUserInput(callback) {
          // TODO(Bug 1598804): remove this once we don't expose anymore the
          // entire test API namespace based on an environment variable.
          if (!Cu.isInAutomation) {
            // This dangerous method should only be available if the
            // automation pref is set, which is the case in browser tests.
            throw new ExtensionUtils.ExtensionError(
              "withHandlingUserInput can only be called in automation"
            );
          }
          ExtensionCommon.withHandlingUserInput(
            context.contentWindow,
            callback
          );
        },

        sendMessage(...args) {
          extension.emit("test-message", ...args);
        },

        notifyPass(msg) {
          extension.emit("test-done", true, msg, getStack(context.getCaller()));
        },

        notifyFail(msg) {
          extension.emit(
            "test-done",
            false,
            msg,
            getStack(context.getCaller())
          );
        },

        log(msg) {
          extension.emit("test-log", true, msg, getStack(context.getCaller()));
        },

        fail(msg) {
          assertTrue(false, msg);
        },

        succeed(msg) {
          assertTrue(true, msg);
        },

        assertTrue(value, msg) {
          assertTrue(value, msg);
        },

        assertFalse(value, msg) {
          assertTrue(!value, msg);
        },

        assertDeepEq(expected, actual, msg) {
          // The bindings generated by Schemas.sys.mjs accepts any input, but the
          // WebIDL-generated binding expects a structurally cloneable input.
          // To ensure consistent behavior regardless of which mechanism was
          // used, verify that the inputs are structurally cloneable.
          // These will throw if the values cannot be cloned.
          function ensureStructurallyCloneable(v) {
            if (typeof v == "object" && v !== null) {
              // Waive xrays to unhide callable members, so that cloneInto will
              // throw if needed.
              v = ChromeUtils.waiveXrays(v);
            }
            new StructuredCloneHolder("test.assertEq", null, v, globalThis);
          }
          // When WebIDL bindings are used, the objects are already cloned
          // structurally, so we don't need to check again.
          if (!context.useWebIDLBindings) {
            ensureStructurallyCloneable(expected);
            ensureStructurallyCloneable(actual);
          }

          extension.emit(
            "test-eq",
            deepEquals(actual, expected),
            String(msg),
            toSource(expected),
            toSource(actual),
            getStack(context.getCaller())
          );
        },

        assertEq(expected, actual, msg) {
          let equal = expected === actual;

          expected = String(expected);
          actual = String(actual);

          if (!equal && expected === actual) {
            actual += " (different)";
          }
          extension.emit(
            "test-eq",
            equal,
            String(msg),
            expected,
            actual,
            getStack(context.getCaller())
          );
        },

        assertRejects(promise, expectedError, msg) {
          // Wrap in a native promise for consistency.
          promise = Promise.resolve(promise);

          return promise.then(
            () => {
              let message = `Promise resolved, expected rejection '${toSource(
                expectedError
              )}'`;
              if (msg) {
                message += `: ${msg}`;
              }
              assertTrue(false, message);
            },
            error => {
              let expected = toSource(expectedError);
              let message = `got '${toSource(error)}'`;
              if (msg) {
                message += `: ${msg}`;
              }

              assertTrue(
                errorMatches(error, expectedError, context),
                `Promise rejected, expecting rejection to match '${expected}', ${message}`
              );
            }
          );
        },

        assertThrows(func, expectedError, msg) {
          if (!expectedError) {
            if (ExtensionCommon.isInWPT) {
              expectedError = /.*/;
            } else {
              throw new ExtensionUtils.ExtensionError(
                "Missing required expectedError"
              );
            }
          }
          try {
            func();

            let message = `Function did not throw, expected error '${toSource(
              expectedError
            )}'`;
            if (msg) {
              message += `: ${msg}`;
            }
            assertTrue(false, message);
          } catch (error) {
            let expected = toSource(expectedError);
            let message = `got '${toSource(error)}'`;
            if (msg) {
              message += `: ${msg}`;
            }

            assertTrue(
              errorMatches(error, expectedError, context),
              `Function threw, expecting error to match '${expected}', ${message}`
            );
          }
        },

        runTests(tests) {
          return runTasks(tests);
        },

        onMessage: new TestEventManager({
          context,
          name: "test.onMessage",
          // TODO bug 1901294: Set resetIdleOnEvent=false. Tests should not be
          // relying on test.onMessage for its side effect of resetting the test
          // but set extensions.background.idle.timeout instead.
          resetIdleOnEvent: true,
          register: fire => {
            let handler = (event, ...args) => {
              fire.async(...args);
            };

            extension.on("test-harness-message", handler);
            return () => {
              extension.off("test-harness-message", handler);
            };
          },
        }).api(),
      },
    };
  }
};
