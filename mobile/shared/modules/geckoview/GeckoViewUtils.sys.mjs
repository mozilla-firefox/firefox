/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { Log } from "resource://gre/modules/Log.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  EventDispatcher: "resource://gre/modules/Messaging.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

if (AppConstants.platform == "android") {
  ChromeUtils.defineESModuleGetters(lazy, {
    AndroidAppender:
      "moz-src:///mobile/android/modules/geckoview/AndroidLog.sys.mjs",
  });
}

export var GeckoViewUtils = {
  /**
   * Define a lazy getter that loads an object from external code, and
   * optionally handles observer and/or message manager notifications for the
   * object, so the object only loads when a notification is received.
   *
   * @param scope     Scope for holding the loaded object.
   * @param name      Name of the object to load.
   * @param service   If specified, load the object from a JS component; the
   *                  component must include the line
   *                  "this.wrappedJSObject = this;" in its constructor.
   * @param module    If specified, load the object from a JS module.
   * @param init      Optional post-load initialization function.
   * @param observers If specified, listen to specified observer notifications.
   * @param ppmm      If specified, listen to specified process messages.
   * @param mm        If specified, listen to specified frame messages.
   * @param ged       If specified, listen to specified global EventDispatcher events.
   * @param once      if true, only listen to the specified
   *                  events/messages/notifications once.
   */
  addLazyGetter(
    scope,
    name,
    { service, module, handler, observers, ppmm, mm, ged, init, once }
  ) {
    ChromeUtils.defineLazyGetter(scope, name, _ => {
      let ret = undefined;
      if (module) {
        ret = ChromeUtils.importESModule(module)[name];
      } else if (service) {
        ret = Cc[service].getService(Ci.nsISupports).wrappedJSObject;
      } else if (typeof handler === "function") {
        ret = {
          handleEvent: handler,
          observe: handler,
          onEvent: handler,
          receiveMessage: handler,
        };
      } else if (handler) {
        ret = handler;
      }
      if (ret && init) {
        init.call(scope, ret);
      }
      return ret;
    });

    if (observers) {
      const observer = (subject, topic, data) => {
        Services.obs.removeObserver(observer, topic);
        if (!once) {
          Services.obs.addObserver(scope[name], topic);
        }
        scope[name].observe(subject, topic, data); // Explicitly notify new observer
      };
      observers.forEach(topic => Services.obs.addObserver(observer, topic));
    }

    if (!this.IS_PARENT_PROCESS) {
      // ppmm, mm, and ged are only available in the parent process.
      return;
    }

    const addMMListener = (target, names) => {
      const listener = msg => {
        target.removeMessageListener(msg.name, listener);
        if (!once) {
          target.addMessageListener(msg.name, scope[name]);
        }
        scope[name].receiveMessage(msg);
      };
      names.forEach(msg => target.addMessageListener(msg, listener));
    };
    if (ppmm) {
      addMMListener(Services.ppmm, ppmm);
    }
    if (mm) {
      addMMListener(Services.mm, mm);
    }

    if (ged) {
      const listener = (event, data, callback) => {
        lazy.EventDispatcher.instance.unregisterListener(listener, event);
        if (!once) {
          lazy.EventDispatcher.instance.registerListener(scope[name], event);
        }
        scope[name].onEvent(event, data, callback);
      };
      lazy.EventDispatcher.instance.registerListener(listener, ged);
    }
  },

  _addLazyListeners(events, handler, scope, name, addFn, handleFn) {
    if (!handler) {
      handler = _ =>
        Array.isArray(name) ? name.map(n => scope[n]) : scope[name];
    }
    const listener = (...args) => {
      let handlers = handler(...args);
      if (!handlers) {
        return;
      }
      if (!Array.isArray(handlers)) {
        handlers = [handlers];
      }
      handleFn(handlers, listener, args);
    };
    if (Array.isArray(events)) {
      addFn(events, listener);
    } else {
      addFn([events], listener);
    }
  },

  /**
   * Add lazy event listeners that only load the actual handler when an event
   * is being handled.
   *
   * @param target  Event target for the event listeners.
   * @param events  Event name as a string or array.
   * @param handler If specified, function that, for a given event, returns the
   *                actual event handler as an object or an array of objects.
   *                If handler is not specified, the actual event handler is
   *                specified using the scope and name pair.
   * @param scope   See handler.
   * @param name    See handler.
   * @param options Options for addEventListener.
   */
  addLazyEventListener(target, events, { handler, scope, name, options }) {
    this._addLazyListeners(
      events,
      handler,
      scope,
      name,
      (events, listener) => {
        events.forEach(event =>
          target.addEventListener(event, listener, options)
        );
      },
      (handlers, listener, args) => {
        if (!options || !options.once) {
          target.removeEventListener(args[0].type, listener, options);
          handlers.forEach(handler =>
            target.addEventListener(args[0].type, handler, options)
          );
        }
        handlers.forEach(handler => handler.handleEvent(args[0]));
      }
    );
  },

  /**
   * Add lazy pref observers, and only load the actual handler once the pref
   * value changes from default, and every time the pref value changes
   * afterwards.
   *
   * @param aPrefs  Prefs as an object or array. Each pref object has fields
   *                "name" and "default", indicating the name and default value
   *                of the pref, respectively.
   * @param handler If specified, function that, for a given pref, returns the
   *                actual event handler as an object or an array of objects.
   *                If handler is not specified, the actual event handler is
   *                specified using the scope and name pair.
   * @param scope   See handler.
   * @param name    See handler.
   * @param once    If true, only observe the specified prefs once.
   */
  addLazyPrefObserver(aPrefs, { handler, scope, name, once }) {
    this._addLazyListeners(
      aPrefs,
      handler,
      scope,
      name,
      (prefs, observer) => {
        prefs.forEach(pref => Services.prefs.addObserver(pref.name, observer));
        prefs.forEach(pref => {
          if (pref.default === undefined) {
            return;
          }
          let value;
          switch (typeof pref.default) {
            case "string":
              value = Services.prefs.getCharPref(pref.name, pref.default);
              break;
            case "number":
              value = Services.prefs.getIntPref(pref.name, pref.default);
              break;
            case "boolean":
              value = Services.prefs.getBoolPref(pref.name, pref.default);
              break;
          }
          if (pref.default !== value) {
            // Notify observer if value already changed from default.
            observer(Services.prefs, "nsPref:changed", pref.name);
          }
        });
      },
      (handlers, observer, args) => {
        if (!once) {
          Services.prefs.removeObserver(args[2], observer);
          handlers.forEach(() => Services.prefs.addObserver(args[2], observer));
        }
        handlers.forEach(handler => handler.observe(...args));
      }
    );
  },

  getRootDocShell(aWin) {
    if (!aWin) {
      return null;
    }
    let docShell;
    try {
      docShell = aWin.QueryInterface(Ci.nsIDocShell);
    } catch (e) {
      docShell = aWin.docShell;
    }
    return docShell.rootTreeItem.QueryInterface(Ci.nsIInterfaceRequestor);
  },

  /**
   * Return the outermost chrome DOM window (the XUL window) for a given DOM
   * window, in the parent process.
   *
   * @param aWin a DOM window.
   */
  getChromeWindow(aWin) {
    const docShell = this.getRootDocShell(aWin);
    return docShell && docShell.domWindow;
  },

  /**
   * Return the per-nsWindow EventDispatcher for a given DOM window, in either
   * the parent process or a child process.
   *
   * @param aWin a DOM window.
   */
  getDispatcherForWindow(aWin) {
    try {
      const win = this.getChromeWindow(aWin.top || aWin);
      if (!win.closed) {
        return win.WindowEventDispatcher || lazy.EventDispatcher.for(win);
      }
    } catch (e) {}
    return null;
  },

  /**
   * Return promise for waiting for finishing PanZoomState.
   *
   * @param aWindow a DOM window.
   * @return promise
   */
  waitForPanZoomState(aWindow) {
    return new Promise((resolve, reject) => {
      if (
        !aWindow?.windowUtils.asyncPanZoomEnabled ||
        !Services.prefs.getBoolPref("apz.zoom-to-focused-input.enabled")
      ) {
        // No zoomToFocusedInput.
        resolve();
        return;
      }

      let timerId = 0;

      const panZoomState = (aSubject, aTopic, aData) => {
        if (timerId != 0) {
          // aWindow may be dead object now.
          try {
            lazy.clearTimeout(timerId);
          } catch (e) {}
          timerId = 0;
        }

        if (aData === "NOTHING") {
          Services.obs.removeObserver(panZoomState, "PanZoom:StateChange");
          resolve();
        }
      };

      Services.obs.addObserver(panZoomState, "PanZoom:StateChange");

      // "GeckoView:ZoomToInput" has the timeout as 500ms when window isn't
      // resized (it means on-screen-keyboard is already shown).
      // So after up to 500ms, APZ event is sent. So we need to wait for more
      // 500ms.
      timerId = lazy.setTimeout(() => {
        // PanZoom state isn't changed. zoomToFocusedInput will return error.
        Services.obs.removeObserver(panZoomState, "PanZoom:StateChange");
        reject();
      }, 600);
    });
  },

  /**
   * Add logging functions to the specified scope that forward to the given
   * Log.sys.mjs logger. Currently "debug" and "warn" functions are supported. To
   * log something, call the function through a template literal:
   *
   *   function foo(bar, baz) {
   *     debug `hello world`;
   *     debug `foo called with ${bar} as bar`;
   *     warn `this is a warning for ${baz}`;
   *   }
   *
   * An inline format can also be used for logging:
   *
   *   let bar = 42;
   *   do_something(bar); // No log.
   *   do_something(debug.foo = bar); // Output "foo = 42" to the log.
   *
   * @param aTag Name of the Log.sys.mjs logger to forward logs to.
   * @param aScope Scope to add the logging functions to.
   */
  initLogging(aTag, aScope) {
    aScope = aScope || {};
    const tag = "GeckoView." + aTag.replace(/^GeckoView\.?/, "");

    // Only provide two levels for simplicity.
    // For "info", use "debug" instead.
    // For "error", throw an actual JS error instead.
    for (const level of ["DEBUG", "WARN"]) {
      const log = (strings, ...exprs) =>
        this._log(log.logger, level, strings, exprs);

      ChromeUtils.defineLazyGetter(log, "logger", _ => {
        const logger = Log.repository.getLogger(tag);
        logger.parent = this.rootLogger;
        return logger;
      });

      aScope[level.toLowerCase()] = new Proxy(log, {
        set: (obj, prop, value) => obj([prop + " = ", ""], value) || true,
      });
    }
    return aScope;
  },

  /**
   * Like initLogging, but forwards to a ConsoleInstance named
   * "GeckoView.<aTag>", whose logModule names its MOZ_LOG module, so
   * about:logging can capture the messages along with their stack traces.
   * Interpolated values are handed to the console as-is rather than formatted
   * into the message, so they stay inspectable.
   *
   * @param aTag Name of the console instance to forward logs to.
   * @param aScope Scope to add the logging functions to.
   */
  initLoggingConsole(aTag, aScope) {
    aScope = aScope || {};
    const tag = "GeckoView." + aTag.replace(/^GeckoView\.?/, "");

    // Deliberately not tied to "geckoview.logging": that pref holds "Fatal"
    // when logging is off, which is not a ConsoleLogLevel. Debug messages are
    // gated on the MOZ_LOG module instead.
    const instance = console.createInstance({
      logModule: tag,
      maxLogLevel: "Warn",
    });

    for (const level of ["debug", "warn"]) {
      const log = (strings, ...exprs) => {
        // Separate console arguments keep logged objects inspectable. A tagged
        // template has one more string than values, hence the trailing slice;
        // the odd indices are the values, kept even when empty.
        const args = strings.flatMap((str, i) => [str.trim(), exprs[i]]);
        instance[level](
          ...args.slice(0, -1).filter((arg, i) => i % 2 || arg !== "")
        );
      };
      aScope[level] = new Proxy(log, {
        set: (obj, prop, value) => obj([prop + " = ", ""], value) || true,
      });
    }
    return aScope;
  },

  get rootLogger() {
    if (!this._rootLogger) {
      this._rootLogger = Log.repository.getLogger("GeckoView");
      // On Android, we'll log to the native android logcat output using
      // __android_log_write. On iOS, fall back to a dump appender.
      if (AppConstants.platform == "android") {
        this._rootLogger.addAppender(new lazy.AndroidAppender());
      } else {
        this._rootLogger.addAppender(new Log.DumpAppender());
      }
      this._rootLogger.manageLevelFromPref("geckoview.logging");
    }
    return this._rootLogger;
  },

  _log(aLogger, aLevel, aStrings, aExprs) {
    if (!Array.isArray(aStrings)) {
      const [, file, line] = new Error().stack.match(/.*\n.*\n.*@(.*):(\d+):/);
      throw Error(
        `Expecting template literal: ${aLevel} \`foo \${bar}\``,
        file,
        +line
      );
    }

    if (aLogger.level > Log.Level.Numbers[aLevel]) {
      // Log disabled.
      return;
    }

    // Do some GeckoView-specific formatting:
    // * Remove newlines so long log lines can be put into multiple lines:
    //   debug `foo=${foo}
    //          bar=${bar}`;
    const strs = Array.from(aStrings);
    const regex = /\n\s*/g;
    for (let i = 0; i < strs.length; i++) {
      strs[i] = strs[i].replace(regex, " ");
    }

    // * Heuristically format flags as hex.
    // * Heuristically format nsresult as string name or hex.
    for (let i = 0; i < aExprs.length; i++) {
      const expr = aExprs[i];
      switch (typeof expr) {
        case "number":
          if (expr > 0 && /\ba?[fF]lags?[\s=:]+$/.test(strs[i])) {
            // Likely a flag; display in hex.
            aExprs[i] = `0x${expr.toString(0x10)}`;
          } else if (expr >= 0 && /\b(a?[sS]tatus|rv)[\s=:]+$/.test(strs[i])) {
            // Likely an nsresult; display in name or hex.
            aExprs[i] = `0x${expr.toString(0x10)}`;
            for (const name in Cr) {
              if (expr === Cr[name]) {
                aExprs[i] = name;
                break;
              }
            }
          }
          break;
      }
    }

    aLogger[aLevel.toLowerCase()](strs, ...aExprs);
  },

  /**
   * Checks whether the principal is supported for permissions.
   *
   * @param {nsIPrincipal} principal
   *        The principal to check.
   *
   * @return {boolean} if the principal is supported.
   */
  isSupportedPermissionsPrincipal(principal) {
    if (!principal) {
      return false;
    }
    if (!(principal instanceof Ci.nsIPrincipal)) {
      throw new Error(
        "Argument passed as principal is not an instance of Ci.nsIPrincipal"
      );
    }
    return this.isSupportedPermissionsScheme(principal.scheme);
  },

  /**
   * Checks whether we support managing permissions for a specific scheme.
   *
   * @param {string} scheme - Scheme to test.
   * @returns {boolean} Whether the scheme is supported.
   */
  isSupportedPermissionsScheme(scheme) {
    return ["http", "https", "moz-extension", "file"].includes(scheme);
  },

  /**
   * Attach nsIOpenWindowInfo when opening GeckoSession
   *
   * @param {string} aSessionId A session id
   * @param {nsIOpenWindowInfo} aOpenWindowInfo Attached nsIOpendWindowInfo
   * @param {string} aName A window name
   * @returns {Promise} resolved when nsIOpenWindowInfo is attached
   */
  waitAndSetupWindow(aSessionId, aOpenWindowInfo, aName) {
    if (!aSessionId) {
      return Promise.reject();
    }

    return new Promise((resolve, reject) => {
      const handler = {
        observe(aSubject, aTopic) {
          if (
            aTopic === "geckoview-window-created" &&
            aSubject.name === aSessionId
          ) {
            // This value will be read by nsFrameLoader while it is being initialized.
            aSubject.browser.openWindowInfo = aOpenWindowInfo;

            // Gecko will use this attribute to set the name of the opened window.
            if (aName) {
              aSubject.browser.setAttribute("name", aName);
            }

            if (
              !aOpenWindowInfo.isRemote &&
              aSubject.browser.hasAttribute("remote")
            ) {
              // We cannot start in remote mode when we have an opener.
              aSubject.browser.removeAttribute("remote");
              aSubject.browser.removeAttribute("remoteType");
            }
            Services.obs.removeObserver(handler, "geckoview-window-created");
            if (!aSubject) {
              reject();
              return;
            }
            resolve(aSubject);
          }
        },
      };

      // This event is emitted from createBrowser() in geckoview.js
      Services.obs.addObserver(handler, "geckoview-window-created");
    });
  },
};

ChromeUtils.defineLazyGetter(
  GeckoViewUtils,
  "IS_PARENT_PROCESS",
  _ => Services.appinfo.processType == Services.appinfo.PROCESS_TYPE_DEFAULT
);
