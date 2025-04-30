/* - This Source Code Form is subject to the terms of the Mozilla Public
   - License, v. 2.0. If a copy of the MPL was not distributed with this file,
   - You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// We attach Preferences to the window object so other contexts (tests, JSMs)
// have access to it.
const Preferences = (window.Preferences = (function () {
  const { EventEmitter } = ChromeUtils.importESModule(
    "resource://gre/modules/EventEmitter.sys.mjs"
  );

  const lazy = {};
  ChromeUtils.defineESModuleGetters(lazy, {
    DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  });

  function getElementsByAttribute(name, value) {
    // If we needed to defend against arbitrary values, we would escape
    // double quotes (") and escape characters (\) in them, i.e.:
    //   ${value.replace(/["\\]/g, '\\$&')}
    return value
      ? document.querySelectorAll(`[${name}="${value}"]`)
      : document.querySelectorAll(`[${name}]`);
  }

  const domContentLoadedPromise = new Promise(resolve => {
    window.addEventListener("DOMContentLoaded", resolve, {
      capture: true,
      once: true,
    });
  });

  const Preferences = {
    _all: {},
    _settings: new Map(),

    _add(prefInfo) {
      if (this._all[prefInfo.id]) {
        throw new Error(`preference with id '${prefInfo.id}' already added`);
      }
      const pref = new Preference(prefInfo);
      this._all[pref.id] = pref;
      domContentLoadedPromise.then(() => {
        if (!this.updateQueued) {
          pref.updateElements();
        }
      });
      return pref;
    },

    add(prefInfo) {
      const pref = this._add(prefInfo);
      return pref;
    },

    addAll(prefInfos) {
      prefInfos.map(prefInfo => this._add(prefInfo));
    },

    get(id) {
      return this._all[id] || null;
    },

    getAll() {
      return Object.values(this._all);
    },

    addSetting(settingConfig) {
      this._settings.set(settingConfig.id, settingConfig);
    },

    getSetting(settingId) {
      return new Setting(settingId, this._settings.get(settingId));
    },

    defaultBranch: Services.prefs.getDefaultBranch(""),

    get type() {
      return document.documentElement.getAttribute("type") || "";
    },

    get instantApply() {
      // The about:preferences page forces instantApply.
      // TODO: Remove forceEnableInstantApply in favor of always applying in a
      // parent and never applying in a child (bug 1775386).
      if (this._instantApplyForceEnabled) {
        return true;
      }

      // Dialogs of type="child" are never instantApply.
      return this.type !== "child";
    },

    _instantApplyForceEnabled: false,

    // Override the computed value of instantApply for this window.
    forceEnableInstantApply() {
      this._instantApplyForceEnabled = true;
    },

    observe(subject, topic, data) {
      const pref = this._all[data];
      if (pref) {
        pref.value = pref.valueFromPreferences;
      }
    },

    updateQueued: false,

    queueUpdateOfAllElements() {
      if (this.updateQueued) {
        return;
      }

      this.updateQueued = true;

      Services.tm.dispatchToMainThread(() => {
        let startTime = performance.now();

        const elements = getElementsByAttribute("preference");
        for (const element of elements) {
          const id = element.getAttribute("preference");
          let preference = this.get(id);
          if (!preference) {
            console.error(`Missing preference for ID ${id}`);
            continue;
          }

          preference.setElementValue(element);
        }

        ChromeUtils.addProfilerMarker(
          "Preferences",
          { startTime },
          `updateAllElements: ${elements.length} preferences updated`
        );

        this.updateQueued = false;
      });
    },

    onUnload() {
      Services.prefs.removeObserver("", this);
    },

    QueryInterface: ChromeUtils.generateQI(["nsITimerCallback", "nsIObserver"]),

    _deferredValueUpdateElements: new Set(),

    writePreferences(aFlushToDisk) {
      // Write all values to preferences.
      if (this._deferredValueUpdateElements.size) {
        this._finalizeDeferredElements();
      }

      const preferences = Preferences.getAll();
      for (const preference of preferences) {
        preference.batching = true;
        preference.valueFromPreferences = preference.value;
        preference.batching = false;
      }
      if (aFlushToDisk) {
        Services.prefs.savePrefFile(null);
      }
    },

    getPreferenceElement(aStartElement) {
      let temp = aStartElement;
      while (
        temp &&
        temp.nodeType == Node.ELEMENT_NODE &&
        !temp.hasAttribute("preference")
      ) {
        temp = temp.parentNode;
      }
      return temp && temp.nodeType == Node.ELEMENT_NODE ? temp : aStartElement;
    },

    _deferredValueUpdate(aElement) {
      delete aElement._deferredValueUpdateTask;
      const prefID = aElement.getAttribute("preference");
      const preference = Preferences.get(prefID);
      const prefVal = preference.getElementValue(aElement);
      preference.value = prefVal;
      this._deferredValueUpdateElements.delete(aElement);
    },

    _finalizeDeferredElements() {
      for (const el of this._deferredValueUpdateElements) {
        if (el._deferredValueUpdateTask) {
          el._deferredValueUpdateTask.finalize();
        }
      }
    },

    userChangedValue(aElement) {
      const element = this.getPreferenceElement(aElement);
      if (element.hasAttribute("preference")) {
        if (element.getAttribute("delayprefsave") != "true") {
          const preference = Preferences.get(
            element.getAttribute("preference")
          );
          const prefVal = preference.getElementValue(element);
          preference.value = prefVal;
        } else {
          if (!element._deferredValueUpdateTask) {
            element._deferredValueUpdateTask = new lazy.DeferredTask(
              this._deferredValueUpdate.bind(this, element),
              1000
            );
            this._deferredValueUpdateElements.add(element);
          } else {
            // Each time the preference is changed, restart the delay.
            element._deferredValueUpdateTask.disarm();
          }
          element._deferredValueUpdateTask.arm();
        }
      }
    },

    onCommand(event) {
      // This "command" event handler tracks changes made to preferences by
      // the user in this window.
      if (event.sourceEvent) {
        event = event.sourceEvent;
      }
      this.userChangedValue(event.target);
    },

    onChange(event) {
      // This "change" event handler tracks changes made to preferences by
      // the user in this window.
      this.userChangedValue(event.target);
    },

    onInput(event) {
      // This "input" event handler tracks changes made to preferences by
      // the user in this window.
      this.userChangedValue(event.target);
    },

    _fireEvent(aEventName, aTarget) {
      try {
        const event = new CustomEvent(aEventName, {
          bubbles: true,
          cancelable: true,
        });
        return aTarget.dispatchEvent(event);
      } catch (e) {
        console.error(e);
      }
      return false;
    },

    onDialogAccept(event) {
      let dialog = document.querySelector("dialog");
      if (!this._fireEvent("beforeaccept", dialog)) {
        event.preventDefault();
        return false;
      }
      this.writePreferences(true);
      return true;
    },

    close(event) {
      if (Preferences.instantApply) {
        window.close();
      }
      event.stopPropagation();
      event.preventDefault();
    },

    handleEvent(event) {
      switch (event.type) {
        case "toggle":
        case "change":
          return this.onChange(event);
        case "command":
          return this.onCommand(event);
        case "dialogaccept":
          return this.onDialogAccept(event);
        case "input":
          return this.onInput(event);
        case "unload":
          return this.onUnload(event);
        default:
          return undefined;
      }
    },

    _syncFromPrefListeners: new WeakMap(),
    _syncToPrefListeners: new WeakMap(),

    addSyncFromPrefListener(aElement, callback) {
      this._syncFromPrefListeners.set(aElement, callback);
      if (this.updateQueued) {
        return;
      }
      // Make sure elements are updated correctly with the listener attached.
      let elementPref = aElement.getAttribute("preference");
      if (elementPref) {
        let pref = this.get(elementPref);
        if (pref) {
          pref.updateElements();
        }
      }
    },

    addSyncToPrefListener(aElement, callback) {
      this._syncToPrefListeners.set(aElement, callback);
      if (this.updateQueued) {
        return;
      }
      // Make sure elements are updated correctly with the listener attached.
      let elementPref = aElement.getAttribute("preference");
      if (elementPref) {
        let pref = this.get(elementPref);
        if (pref) {
          pref.updateElements();
        }
      }
    },

    removeSyncFromPrefListener(aElement) {
      this._syncFromPrefListeners.delete(aElement);
    },

    removeSyncToPrefListener(aElement) {
      this._syncToPrefListeners.delete(aElement);
    },
  };

  Services.prefs.addObserver("", Preferences);
  window.addEventListener("toggle", Preferences);
  window.addEventListener("change", Preferences);
  window.addEventListener("command", Preferences);
  window.addEventListener("dialogaccept", Preferences);
  window.addEventListener("input", Preferences);
  window.addEventListener("select", Preferences);
  window.addEventListener("unload", Preferences, { once: true });

  class Preference extends EventEmitter {
    constructor({ id, type, inverted }) {
      super();
      this.on("change", this.onChange.bind(this));

      this._value = null;
      this.readonly = false;
      this._useDefault = false;
      this.batching = false;

      this.id = id;
      this.type = type;
      this.inverted = !!inverted;

      // In non-instant apply mode, we must try and use the last saved state
      // from any previous opens of a child dialog instead of the value from
      // preferences, to pick up any edits a user may have made.

      if (
        Preferences.type == "child" &&
        window.opener &&
        window.opener.Preferences &&
        window.opener.document.nodePrincipal.isSystemPrincipal
      ) {
        // Try to find the preference in the parent window.
        const preference = window.opener.Preferences.get(this.id);

        // Don't use the value setter here, we don't want updateElements to be
        // prematurely fired.
        this._value = preference ? preference.value : this.valueFromPreferences;
      } else {
        this._value = this.valueFromPreferences;
      }
    }

    reset() {
      // defer reset until preference update
      this.value = undefined;
    }

    _reportUnknownType() {
      const msg = `Preference with id=${this.id} has unknown type ${this.type}.`;
      Services.console.logStringMessage(msg);
    }

    setElementValue(aElement) {
      if (this.locked) {
        aElement.disabled = true;
      }
      if (aElement.labels?.length) {
        for (let label of aElement.labels) {
          label.toggleAttribute("disabled", this.locked);
        }
      }

      if (!this.isElementEditable(aElement)) {
        return;
      }

      let rv = undefined;

      if (Preferences._syncFromPrefListeners.has(aElement)) {
        rv = Preferences._syncFromPrefListeners.get(aElement)(aElement);
      }
      let val = rv;
      if (val === undefined) {
        val = Preferences.instantApply ? this.valueFromPreferences : this.value;
      }
      // if the preference is marked for reset, show default value in UI
      if (val === undefined) {
        val = this.defaultValue;
      }

      /**
       * Initialize a UI element property with a value. Handles the case
       * where an element has not yet had a XBL binding attached for it and
       * the property setter does not yet exist by setting the same attribute
       * on the XUL element using DOM apis and assuming the element's
       * constructor or property getters appropriately handle this state.
       */
      function setValue(element, attribute, value) {
        if (attribute in element) {
          element[attribute] = value;
        } else if (attribute === "checked" || attribute === "pressed") {
          // The "checked" attribute can't simply be set to the specified value;
          // it has to be set if the value is true and removed if the value
          // is false in order to be interpreted correctly by the element.
          if (value) {
            // In theory we can set it to anything; however xbl implementation
            // of `checkbox` only works with "true".
            element.setAttribute(attribute, "true");
          } else {
            element.removeAttribute(attribute);
          }
        } else {
          element.setAttribute(attribute, value);
        }
      }
      if (
        aElement.localName == "checkbox" ||
        aElement.localName == "moz-checkbox" ||
        (aElement.localName == "input" && aElement.type == "checkbox")
      ) {
        setValue(aElement, "checked", val);
      } else if (aElement.localName == "moz-toggle") {
        setValue(aElement, "pressed", val);
      } else {
        setValue(aElement, "value", val);
      }
    }

    getElementValue(aElement) {
      if (Preferences._syncToPrefListeners.has(aElement)) {
        try {
          const rv = Preferences._syncToPrefListeners.get(aElement)(aElement);
          if (rv !== undefined) {
            return rv;
          }
        } catch (e) {
          console.error(e);
        }
      }

      /**
       * Read the value of an attribute from an element, assuming the
       * attribute is a property on the element's node API. If the property
       * is not present in the API, then assume its value is contained in
       * an attribute, as is the case before a binding has been attached.
       */
      function getValue(element, attribute) {
        if (attribute in element) {
          return element[attribute];
        }
        return element.getAttribute(attribute);
      }
      let value;
      if (
        aElement.localName == "checkbox" ||
        aElement.localName == "moz-checkbox" ||
        (aElement.localName == "input" && aElement.type == "checkbox")
      ) {
        value = getValue(aElement, "checked");
      } else if (aElement.localName == "moz-toggle") {
        value = getValue(aElement, "pressed");
      } else {
        value = getValue(aElement, "value");
      }

      switch (this.type) {
        case "int":
          return parseInt(value, 10) || 0;
        case "bool":
          return typeof value == "boolean" ? value : value == "true";
      }
      return value;
    }

    isElementEditable(aElement) {
      switch (aElement.localName) {
        case "checkbox":
        case "input":
        case "radiogroup":
        case "textarea":
        case "menulist":
        case "moz-toggle":
        case "moz-checkbox":
          return true;
      }
      return false;
    }

    updateElements() {
      let startTime = performance.now();

      if (!this.id) {
        return;
      }

      const elements = getElementsByAttribute("preference", this.id);
      for (const element of elements) {
        this.setElementValue(element);
      }

      ChromeUtils.addProfilerMarker(
        "Preferences",
        { startTime, captureStack: true },
        `updateElements for ${this.id}`
      );
    }

    onChange() {
      this.updateElements();
    }

    get value() {
      return this._value;
    }

    set value(val) {
      if (this.value !== val) {
        this._value = val;
        if (Preferences.instantApply) {
          this.valueFromPreferences = val;
        }
        this.emit("change");
      }
    }

    get locked() {
      return Services.prefs.prefIsLocked(this.id);
    }

    updateControlDisabledState(val) {
      if (!this.id) {
        return;
      }

      val = val || this.locked;

      const elements = getElementsByAttribute("preference", this.id);
      for (const element of elements) {
        element.disabled = val;

        const labels = getElementsByAttribute("control", element.id);
        for (const label of labels) {
          label.disabled = val;
        }
      }
    }

    get hasUserValue() {
      return (
        Services.prefs.prefHasUserValue(this.id) && this.value !== undefined
      );
    }

    get defaultValue() {
      this._useDefault = true;
      const val = this.valueFromPreferences;
      this._useDefault = false;
      return val;
    }

    get _branch() {
      return this._useDefault ? Preferences.defaultBranch : Services.prefs;
    }

    get valueFromPreferences() {
      try {
        // Force a resync of value with preferences.
        switch (this.type) {
          case "int":
            return this._branch.getIntPref(this.id);
          case "bool": {
            const val = this._branch.getBoolPref(this.id);
            return this.inverted ? !val : val;
          }
          case "wstring":
            return this._branch.getComplexValue(
              this.id,
              Ci.nsIPrefLocalizedString
            ).data;
          case "string":
          case "unichar":
            return this._branch.getStringPref(this.id);
          case "fontname": {
            const family = this._branch.getStringPref(this.id);
            const fontEnumerator = Cc[
              "@mozilla.org/gfx/fontenumerator;1"
            ].createInstance(Ci.nsIFontEnumerator);
            return fontEnumerator.getStandardFamilyName(family);
          }
          case "file": {
            const f = this._branch.getComplexValue(this.id, Ci.nsIFile);
            return f;
          }
          default:
            this._reportUnknownType();
        }
      } catch (e) {}
      return null;
    }

    set valueFromPreferences(val) {
      // Exit early if nothing to do.
      if (this.readonly || this.valueFromPreferences == val) {
        return;
      }

      // The special value undefined means 'reset preference to default'.
      if (val === undefined) {
        Services.prefs.clearUserPref(this.id);
        return;
      }

      // Force a resync of preferences with value.
      switch (this.type) {
        case "int":
          Services.prefs.setIntPref(this.id, val);
          break;
        case "bool":
          Services.prefs.setBoolPref(this.id, this.inverted ? !val : val);
          break;
        case "wstring": {
          const pls = Cc["@mozilla.org/pref-localizedstring;1"].createInstance(
            Ci.nsIPrefLocalizedString
          );
          pls.data = val;
          Services.prefs.setComplexValue(
            this.id,
            Ci.nsIPrefLocalizedString,
            pls
          );
          break;
        }
        case "string":
        case "unichar":
        case "fontname":
          Services.prefs.setStringPref(this.id, val);
          break;
        case "file": {
          let lf;
          if (typeof val == "string") {
            lf = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
            lf.persistentDescriptor = val;
            if (!lf.exists()) {
              lf.initWithPath(val);
            }
          } else {
            lf = val.QueryInterface(Ci.nsIFile);
          }
          Services.prefs.setComplexValue(this.id, Ci.nsIFile, lf);
          break;
        }
        default:
          this._reportUnknownType();
      }
      if (!this.batching) {
        Services.prefs.savePrefFile(null);
      }
    }
  }

  class Setting extends EventEmitter {
    constructor(id, config) {
      super();
      this.id = id;
      this.config = config;
      this.pref = config.pref && Preferences.get(config.pref);
      if (this.pref) {
        this.pref.on("change", this.onChange);
      }
    }

    onChange = () => {
      this.emit("change");
    };

    get value() {
      let prefVal = this.pref?.value;
      if (this.config.get) {
        return this.config.get(prefVal);
      }
      return prefVal;
    }

    set value(val) {
      let newVal = this.config.set ? this.config.set(val) : val;
      if (this.pref) {
        this.pref.value = newVal;
      }
    }

    get visible() {
      return this.config.visible ? this.config.visible() : true;
    }

    userChange(val) {
      this.value = val;
      if (this.config.onUserChange) {
        this.config.onUserChange(val);
      }
    }
  }

  return Preferences;
})());
