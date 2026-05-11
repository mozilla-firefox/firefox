/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var { DelayedInit } = ChromeUtils.importESModule(
  "resource://gre/modules/DelayedInit.sys.mjs"
);
var { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  Blocklist: "resource://gre/modules/Blocklist.sys.mjs",
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
  EventDispatcher: "resource://gre/modules/Messaging.sys.mjs",
  GeckoViewActorManager: "resource://gre/modules/GeckoViewActorManager.sys.mjs",
  GeckoViewSettings: "resource://gre/modules/GeckoViewSettings.sys.mjs",
  GeckoViewUtils: "resource://gre/modules/GeckoViewUtils.sys.mjs",
  InitializationTracker: "resource://gre/modules/GeckoViewTelemetry.sys.mjs",
  RemoteSecuritySettings:
    "resource://gre/modules/psm/RemoteSecuritySettings.sys.mjs",
  SafeBrowsing: "resource://gre/modules/SafeBrowsing.sys.mjs",
  CaptchaDetectionPingUtils:
    "resource://gre/modules/CaptchaDetectionPingUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(this, "WindowEventDispatcher", () =>
  EventDispatcher.for(window)
);

XPCOMUtils.defineLazyScriptGetter(
  this,
  "PrintUtils",
  "chrome://global/content/printUtils.js"
);

// This file assumes `warn` and `debug` are imported into scope
// by the child scripts.
/* global debug, warn */

/**
 * ModuleManager creates and manages GeckoView modules. Each GeckoView module
 * normally consists of a JSM module file with an optional content module file.
 * The module file contains a class that extends GeckoViewModule. A
 * module usually pairs with a particular GeckoSessionHandler or delegate on the
 * Java side, and automatically receives module lifetime events such as
 * initialization, change in enabled state, and change in settings.
 */
var ModuleManager = {
  get _initData() {
    return window.arguments[0].QueryInterface(Ci.nsIGeckoViewView).initData;
  },

  init(aBrowser, aModules) {
    const initData = this._initData;
    this._browser = aBrowser;
    this._settings = initData.settings;
    this._frozenSettings = Object.freeze(Object.assign({}, this._settings));

    // Promise resolvers set during applink navigation, used to defer extension startup.
    this._applinkNavigation = null;

    const self = this;
    this._modules = new Map(
      (function* () {
        for (const module of aModules) {
          yield [
            module.name,
            new ModuleInfo({
              enabled: !!initData.modules[module.name],
              manager: self,
              ...module,
            }),
          ];
        }
      })()
    );

    window.document.documentElement.appendChild(aBrowser);

    // By default all layers are discarded when a browser is set to inactive.
    // GeckoView by default sets browsers to inactive every time they're not
    // visible. To avoid flickering when changing tabs, we preserve layers for
    // all loaded tabs.
    aBrowser.preserveLayers(true);
    // GeckoView browsers start off as active (for now at least).
    // See bug 1815015 for an attempt at making them start off inactive.
    aBrowser.docShellIsActive = true;

    WindowEventDispatcher.registerListener(this, [
      "GeckoView:UpdateModuleState",
      "GeckoView:UpdateInitData",
      "GeckoView:UpdateSettings",
    ]);

    this._moduleByActorName = new Map();
    this.forEach(module => {
      module.onInit();
      for (const actorName of module.actorNames) {
        this._moduleByActorName[actorName] = module;
      }
    });

    window.addEventListener("unload", () => {
      this.forEach(module => {
        module.enabled = false;
        module.onDestroy();
      });

      this._modules.clear();
    });
  },

  onPrintWindow(aParams) {
    if (!aParams.openWindowInfo.isForWindowDotPrint) {
      return PrintUtils.handleStaticCloneCreatedForPrint(
        aParams.openWindowInfo
      );
    }
    const printActor = this.window.moduleManager.getActor(
      "GeckoViewPrintDelegate"
    );
    // Prevents continually making new static browsers
    if (printActor.browserStaticClone != null) {
      throw new Error("A prior window.print is still in progress.");
    }
    const staticBrowser = PrintUtils.createParentBrowserForStaticClone(
      aParams.openWindowInfo.parent,
      aParams.openWindowInfo
    );
    printActor.browserStaticClone = staticBrowser;
    printActor.printRequest();
    return staticBrowser;
  },

  get window() {
    return window;
  },

  get browser() {
    return this._browser;
  },

  get eventDispatcher() {
    return WindowEventDispatcher;
  },

  get settings() {
    return this._frozenSettings;
  },

  forEach(aCallback) {
    this._modules.forEach(aCallback, this);
  },

  getActor(aActorName) {
    return this.browser.browsingContext.currentWindowGlobal?.getActor(
      aActorName
    );
  },

  // Defer a notification callback if an applink navigation is in progress,
  // waiting for the navigation to complete (or a timeout) before idle-
  // dispatching the callback. Fires immediately if no applink is pending.
  _maybeDeferForApplink(notifyFn, timeoutMs = 5000) {
    const applinkNav = this._applinkNavigation;
    if (applinkNav) {
      const timeoutPromise = new Promise(resolve => {
        setTimeout(resolve, timeoutMs);
      });
      Promise.race([applinkNav.promise, timeoutPromise]).then(() => {
        this._applinkNavigation = null;
        Services.tm.idleDispatchToMainThread(notifyFn);
      });
    } else {
      notifyFn();
    }
  },

  willChangeBrowserRemoteness() {
    debug`WillChangeBrowserRemoteness`;

    // Now we're switching the remoteness.
    this.forEach(module => {
      module.onDestroyBrowser();
    });
  },

  didChangeBrowserRemoteness() {
    debug`DidChangeBrowserRemoteness`;

    this.forEach(module => {
      if (module.impl) {
        module.impl.onInitBrowser();
      }
    });
  },

  _updateSettings(aSettings) {
    Object.assign(this._settings, aSettings);
    this._frozenSettings = Object.freeze(Object.assign({}, this._settings));

    const windowType = aSettings.isExtensionPopup
      ? "navigator:popup"
      : "navigator:geckoview";
    window.document.documentElement.setAttribute("windowtype", windowType);

    this.forEach(module => {
      if (module.impl) {
        module.impl.onSettingsUpdate();
      }
    });
  },

  onMessageFromActor(aActorName, aMessage) {
    this._moduleByActorName[aActorName].receiveMessage(aMessage);
  },

  onEvent(aEvent, aData) {
    debug`onEvent ${aEvent} ${aData}`;
    switch (aEvent) {
      case "GeckoView:UpdateModuleState": {
        const module = this._modules.get(aData.module);
        if (module) {
          module.enabled = aData.enabled;
        }
        break;
      }

      case "GeckoView:UpdateInitData": {
        // Replace all settings during a transfer.
        const initData = this._initData;
        this._updateSettings(initData.settings);

        // Update module enabled states.
        for (const name in initData.modules) {
          const module = this._modules.get(name);
          if (module) {
            module.enabled = initData.modules[name];
          }
        }
        break;
      }

      case "GeckoView:UpdateSettings": {
        this._updateSettings(aData);
        break;
      }
    }
  },
};

/**
 * ModuleInfo is the structure used by ModuleManager to represent individual
 * modules. It is responsible for loading the module JSM file if necessary,
 * and it acts as the intermediary between ModuleManager and the module
 * object that extends GeckoViewModule.
 */
class ModuleInfo {
  /**
   * Create a ModuleInfo instance. See _loadPhase for phase object description.
   *
   * @param manager the ModuleManager instance.
   * @param name Name of the module.
   * @param enabled Enabled state of the module at startup.
   * @param onInit Phase object for the init phase, when the window is created.
   * @param onEnable Phase object for the enable phase, when the module is first
   *                 enabled by setting a delegate in Java.
   */
  constructor({ manager, name, enabled, onInit, onEnable }) {
    this._manager = manager;
    this._name = name;

    // We don't support having more than one main process script, so let's
    // check that we're not accidentally defining two. We could support this if
    // needed by making _impl an array for each phase impl.
    if (onInit?.resource !== undefined && onEnable?.resource !== undefined) {
      throw new Error(
        "Only one main process script is allowed for each module."
      );
    }

    this._impl = null;
    this._contentModuleLoaded = false;
    this._enabled = false;
    // Only enable once we performed initialization.
    this._enabledOnInit = enabled;

    // For init, load resource _before_ initializing browser to support the
    // onInitBrowser() override. However, load content module after initializing
    // browser, because we don't have a message manager before then.
    this._loadResource(onInit);
    this._loadActors(onInit);
    if (this._enabledOnInit) {
      this._loadActors(onEnable);
    }

    this._onInitPhase = onInit;
    this._onEnablePhase = onEnable;

    const actorNames = [];
    if (this._onInitPhase?.actors) {
      actorNames.push(Object.keys(this._onInitPhase.actors));
    }
    if (this._onEnablePhase?.actors) {
      actorNames.push(Object.keys(this._onEnablePhase.actors));
    }
    this._actorNames = Object.freeze(actorNames);
  }

  get actorNames() {
    return this._actorNames;
  }

  onInit() {
    if (this._impl) {
      this._impl.onInit();
      this._impl.onSettingsUpdate();
    }

    this.enabled = this._enabledOnInit;
  }

  onDestroy() {
    if (this._impl) {
      this._impl.onDestroy();
    }
  }

  /**
   * Called before the browser is removed
   */
  onDestroyBrowser() {
    if (this._impl) {
      this._impl.onDestroyBrowser();
    }
    this._contentModuleLoaded = false;
  }

  _loadActors(aPhase) {
    if (!aPhase || !aPhase.actors) {
      return;
    }

    GeckoViewActorManager.addJSWindowActors(aPhase.actors);
  }

  /**
   * Load resource according to a phase object that contains possible keys,
   *
   * "resource": specify the JSM resource to load for this module.
   */
  _loadResource(aPhase) {
    if (!aPhase || !aPhase.resource || this._impl) {
      return;
    }

    const exports = ChromeUtils.importESModule(aPhase.resource);
    this._impl = new exports[this._name](this);
  }

  get manager() {
    return this._manager;
  }

  get name() {
    return this._name;
  }

  get impl() {
    return this._impl;
  }

  get enabled() {
    return this._enabled;
  }

  set enabled(aEnabled) {
    if (aEnabled === this._enabled) {
      return;
    }

    if (!aEnabled && this._impl) {
      this._impl.onDisable();
    }

    this._enabled = aEnabled;

    if (aEnabled) {
      this._loadResource(this._onEnablePhase);
      this._loadActors(this._onEnablePhase);
      if (this._impl) {
        this._impl.onEnable();
        this._impl.onSettingsUpdate();
      }
    }
  }

  receiveMessage(aMessage) {
    if (!this._impl) {
      throw new Error(`No impl for message: ${aMessage.name}.`);
    }

    try {
      this._impl.receiveMessage(aMessage);
    } catch (error) {
      warn`this._impl.receiveMessage failed ${aMessage.name}`;
      throw error;
    }
  }
}

function createBrowser() {
  const browser = (window.browser = document.createXULElement("browser"));
  // Identify this `<browser>` element uniquely to Marionette, devtools, etc.
  // Use the JSM global to create the permanentKey, so that if the
  // permanentKey is held by something after this window closes, it
  // doesn't keep the window alive. See also Bug 1501789.
  browser.permanentKey = new (Cu.getGlobalForObject(Services).Object)();

  browser.setAttribute("nodefaultsrc", "true");
  browser.setAttribute("type", "content");
  browser.setAttribute("primary", "true");
  browser.setAttribute("flex", "1");
  browser.setAttribute("maychangeremoteness", "true");
  browser.setAttribute("remote", "true");
  browser.setAttribute("remoteType", E10SUtils.DEFAULT_REMOTE_TYPE);
  browser.setAttribute("messagemanagergroup", "browsers");
  browser.setAttribute("manualactiveness", "true");

  // This is only needed for mochitests, so that they honor the
  // prefers-color-scheme.content-override pref. GeckoView doesn't set this
  // pref to anything other than the default value otherwise.
  browser.style.colorScheme = "env(-moz-content-preferred-color-scheme)";

  return browser;
}

function InitLater(fn, object, name) {
  return DelayedInit.schedule(fn, object, name, 15000 /* 15s max wait */);
}

function startup() {
  GeckoViewUtils.initLogging("XUL", window);

  const browser = createBrowser();
  ModuleManager.init(browser, [
    {
      name: "GeckoViewContent",
      onInit: {
        resource: "resource://gre/modules/GeckoViewContent.sys.mjs",
        actors: {
          GeckoViewContent: {
            parent: {
              esModuleURI: "resource:///actors/GeckoViewContentParent.sys.mjs",
            },
            child: {
              esModuleURI: "resource:///actors/GeckoViewContentChild.sys.mjs",
              events: {
                mozcaretstatechanged: { capture: true, mozSystemGroup: true },
                pageshow: { mozSystemGroup: true },
              },
            },
            allFrames: true,
            messageManagerGroups: ["browsers"],
          },
        },
      },
      onEnable: {
        actors: {
          ContentDelegate: {
            parent: {
              esModuleURI: "resource:///actors/ContentDelegateParent.sys.mjs",
            },
            child: {
              esModuleURI: "resource:///actors/ContentDelegateChild.sys.mjs",
              events: {
                DOMContentLoaded: {},
                DOMMetaViewportFitChanged: {},
                "MozDOMFullscreen:Entered": {},
                "MozDOMFullscreen:Exit": {},
                "MozDOMFullscreen:Exited": {},
                "MozDOMFullscreen:Request": {},
                MozFirstContentfulPaint: {},
                MozPaintStatusReset: {},
                contextmenu: {},
              },
            },
            allFrames: true,
            messageManagerGroups: ["browsers"],
          },
        },
      },
    },
    {
      name: "GeckoViewNavigation",
      onInit: {
        resource: "resource://gre/modules/GeckoViewNavigation.sys.mjs",
      },
    },
    {
      name: "GeckoViewProcessHangMonitor",
      onInit: {
        resource: "resource://gre/modules/GeckoViewProcessHangMonitor.sys.mjs",
      },
    },
    {
      name: "GeckoViewProgress",
      onEnable: {
        resource: "resource://gre/modules/GeckoViewProgress.sys.mjs",
        actors: {
          ProgressDelegate: {
            parent: {
              esModuleURI: "resource:///actors/ProgressDelegateParent.sys.mjs",
            },
            child: {
              esModuleURI: "resource:///actors/ProgressDelegateChild.sys.mjs",
              events: {
                MozAfterPaint: { capture: false, mozSystemGroup: true },
                DOMContentLoaded: { capture: false, mozSystemGroup: true },
                pageshow: { capture: false, mozSystemGroup: true },
              },
            },
            messageManagerGroups: ["browsers"],
          },
        },
      },
    },
    {
      name: "GeckoViewScroll",
      onEnable: {
        actors: {
          ScrollDelegate: {
            parent: {
              esModuleURI: "resource:///actors/ScrollDelegateParent.sys.mjs",
            },
            child: {
              esModuleURI: "resource:///actors/ScrollDelegateChild.sys.mjs",
              events: {
                mozvisualscroll: { mozSystemGroup: true },
              },
            },
            messageManagerGroups: ["browsers"],
          },
        },
      },
    },
    {
      name: "GeckoViewSelectionAction",
      onEnable: {
        resource: "resource://gre/modules/GeckoViewSelectionAction.sys.mjs",
        actors: {
          SelectionActionDelegate: {
            parent: {
              esModuleURI:
                "resource:///actors/SelectionActionDelegateParent.sys.mjs",
            },
            child: {
              esModuleURI:
                "resource:///actors/SelectionActionDelegateChild.sys.mjs",
              events: {
                mozcaretstatechanged: { mozSystemGroup: true },
                pagehide: { capture: true, mozSystemGroup: true },
                deactivate: { mozSystemGroup: true },
              },
            },
            allFrames: true,
            messageManagerGroups: ["browsers"],
          },
        },
      },
    },
    {
      name: "GeckoViewSettings",
      onInit: {
        resource: "resource://gre/modules/GeckoViewSettings.sys.mjs",
        actors: {
          GeckoViewSettings: {
            child: {
              esModuleURI: "resource:///actors/GeckoViewSettingsChild.sys.mjs",
            },
          },
        },
      },
    },
    {
      name: "GeckoViewTab",
      onInit: {
        resource: "resource://gre/modules/GeckoViewTab.sys.mjs",
      },
    },
    {
      name: "GeckoViewContentBlocking",
      onInit: {
        resource: "resource://gre/modules/GeckoViewContentBlocking.sys.mjs",
      },
    },
    {
      name: "GeckoViewAutofill",
      onInit: {
        actors: {
          GeckoViewAutoFill: {
            parent: {
              esModuleURI: "resource:///actors/GeckoViewAutoFillParent.sys.mjs",
            },
            child: {
              esModuleURI: "resource:///actors/GeckoViewAutoFillChild.sys.mjs",
              events: {
                DOMFormHasPassword: {
                  mozSystemGroup: true,
                  capture: false,
                },
                DOMInputPasswordAdded: {
                  mozSystemGroup: true,
                  capture: false,
                },
                pagehide: {
                  mozSystemGroup: true,
                  capture: false,
                },
                pageshow: {
                  mozSystemGroup: true,
                  capture: false,
                },
                focusin: {
                  mozSystemGroup: true,
                  capture: false,
                },
                focusout: {
                  mozSystemGroup: true,
                  capture: false,
                },
                "PasswordManager:ShowDoorhanger": {},
              },
            },
            allFrames: true,
            messageManagerGroups: ["browsers"],
          },
        },
      },
    },
    {
      name: "GeckoViewMediaControl",
      onEnable: {
        resource: "resource://gre/modules/GeckoViewMediaControl.sys.mjs",
        actors: {
          MediaControlDelegate: {
            parent: {
              esModuleURI:
                "resource:///actors/MediaControlDelegateParent.sys.mjs",
            },
            child: {
              esModuleURI:
                "resource:///actors/MediaControlDelegateChild.sys.mjs",
              events: {
                "MozDOMFullscreen:Entered": {},
                "MozDOMFullscreen:Exited": {},
              },
            },
            allFrames: true,
            messageManagerGroups: ["browsers"],
          },
        },
      },
    },
    {
      name: "GeckoViewAutocomplete",
      onInit: {
        actors: {
          FormAutofill: {
            parent: {
              esModuleURI: "resource://autofill/FormAutofillParent.sys.mjs",
            },
            child: {
              esModuleURI: "resource://autofill/FormAutofillChild.sys.mjs",
              events: {
                focusin: {},
                "form-changed": {},
                "form-submission-detected": {},
              },
            },
            allFrames: true,
            messageManagerGroups: ["browsers"],
          },
        },
      },
    },
    {
      name: "GeckoViewPrompter",
      onInit: {
        actors: {
          GeckoViewPrompter: {
            parent: {
              esModuleURI: "resource:///actors/GeckoViewPrompterParent.sys.mjs",
            },
            child: {
              esModuleURI: "resource:///actors/GeckoViewPrompterChild.sys.mjs",
            },
            allFrames: true,
            includeChrome: true,
          },
        },
      },
    },
    {
      name: "GeckoViewPrintDelegate",
      onInit: {
        actors: {
          GeckoViewPrintDelegate: {
            parent: {
              esModuleURI:
                "resource:///actors/GeckoViewPrintDelegateParent.sys.mjs",
            },
            child: {
              esModuleURI:
                "resource:///actors/GeckoViewPrintDelegateChild.sys.mjs",
            },
            allFrames: true,
          },
        },
      },
    },
    {
      name: "GeckoViewExperimentDelegate",
      onInit: {
        actors: {
          GeckoViewExperimentDelegate: {
            parent: {
              esModuleURI:
                "resource:///actors/GeckoViewExperimentDelegateParent.sys.mjs",
            },
            allFrames: true,
          },
        },
      },
    },
    {
      name: "GeckoViewTranslations",
      onInit: {
        resource: "resource://gre/modules/GeckoViewTranslations.sys.mjs",
      },
    },
    {
      name: "GeckoViewPageExtractor",
      onInit: {
        resource: "resource://gre/modules/GeckoViewPageExtractor.sys.mjs",
      },
    },
  ]);

  browser.addEventListener("WillChangeBrowserRemoteness", () =>
    ModuleManager.willChangeBrowserRemoteness()
  );

  browser.addEventListener("DidChangeBrowserRemoteness", () =>
    ModuleManager.didChangeBrowserRemoteness()
  );

  // Allows actors to access ModuleManager.
  window.moduleManager = ModuleManager;

  window.prompts = () => {
    return window.ModuleManager.getActor("GeckoViewPrompter").getPrompts();
  };

  Services.tm.dispatchToMainThread(() => {
    // This should always be the first thing we do here - any additional delayed
    // initialisation tasks should be added between "browser-delayed-startup-finished"
    // and "browser-idle-startup-tasks-finished".

    // Bug 1496684: Various bits of platform stuff depend on this notification
    // to learn when a browser window has finished its initial (chrome)
    // initialisation, especially with regards to the very first window that is
    // created. Therefore, GeckoView "windows" need to send this, too.
    InitLater(() =>
      Services.obs.notifyObservers(window, "browser-delayed-startup-finished")
    );

    // Let the extension code know it can start loading things that were delayed
    // while GeckoView started up.
    InitLater(() => {
      window.moduleManager._maybeDeferForApplink(() => {
        Services.obs.notifyObservers(window, "extensions-late-startup");
      });
    });

    InitLater(() => {
      // TODO bug 1730026: this runs too often. It should run once.
      RemoteSecuritySettings.init();
    });

    InitLater(() => {
      // Initialize safe browsing module. This is required for content
      // blocking features and manages blocklist downloads and updates.
      SafeBrowsing.init();
    });

    InitLater(() => {
      // It's enough to run this once to set up FOG.
      // (See also bug 1730026.)
      Services.fog.registerCustomPings();
    });

    InitLater(() => {
      // Initialize the blocklist module.
      // TODO bug 1730026: this runs too often. It should run once.
      Blocklist.loadBlocklistAsync();
    });

    InitLater(() => {
      // Call the init function for the CaptchaDetectionPingUtils module.
      // This function adds pref observers that flushes the ping. It also
      // submits the ping if it has data and has been about 24 hours since the
      // last submission.
      CaptchaDetectionPingUtils.init();
    });

    // This should always go last, since the idle tasks (except for the ones with
    // timeouts) should execute in order. Note that this observer notification is
    // not guaranteed to fire, since the window could close before we get here.

    // This notification in particular signals the ScriptPreloader that we have
    // finished startup, so it can now stop recording script usage and start
    // updating the startup cache for faster script loading.
    InitLater(() =>
      Services.obs.notifyObservers(
        window,
        "browser-idle-startup-tasks-finished"
      )
    );
  });

  // Move focus to the content window at the end of startup,
  // so things like text selection can work properly.
  browser.focus();

  InitializationTracker.onInitialized(ChromeUtils.now());
}

window.addEventListener("DOMContentLoaded", startup, { once: true });
