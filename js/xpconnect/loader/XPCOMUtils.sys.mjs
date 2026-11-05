/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

let global = Cu.getGlobalForObject({});

// Some global imports expose additional symbols; for example,
// `Cu.importGlobalProperties(["MessageChannel"])` imports `MessageChannel`
// and `MessagePort`. This table maps those extra symbols to the main
// import name.
const EXTRA_GLOBAL_NAME_TO_IMPORT_NAME = {
  MessagePort: "MessageChannel",
};

/**
 * Redefines the given property on the given object with the given
 * value. This can be used to redefine getter properties which do not
 * implement setters.
 */
function redefine(object, prop, value) {
  Object.defineProperty(object, prop, {
    configurable: true,
    enumerable: true,
    value,
    writable: true,
  });
  return value;
}

/**
 * XPCOMUtils contains helpers to make lazily loading scripts, modules, prefs
 * and XPCOM services more ergonomic for JS consumers.
 *
 * @class
 */
export var XPCOMUtils = {
  /**
   * Defines a getter on a specified object for a script.  The script will not
   * be loaded until first use.
   *
   * @param {object} aObject
   *        The object to define the lazy getter on.
   * @param {string|string[]} aNames
   *        The name of the getter to define on aObject for the script.
   *        This can be a string if the script exports only one symbol,
   *        or an array of strings if the script can be first accessed
   *        from several different symbols.
   * @param {string} aResource
   *        The URL used to obtain the script.
   */
  defineLazyScriptGetter(aObject, aNames, aResource) {
    if (!Array.isArray(aNames)) {
      aNames = [aNames];
    }
    for (let name of aNames) {
      Object.defineProperty(aObject, name, {
        get() {
          XPCOMUtils._scriptloader.loadSubScript(aResource, aObject);
          return aObject[name];
        },
        set(value) {
          redefine(aObject, name, value);
        },
        configurable: true,
        enumerable: true,
      });
    }
  },

  /**
   * Overrides the scriptloader definition for tests to help with globals
   * tracking. Should only be used for tests.
   *
   * @param {object} aObject
   *        The alternative script loader object to use.
   */
  overrideScriptLoaderForTests(aObject) {
    Cu.crashIfNotInAutomation();
    delete this._scriptloader;
    this._scriptloader = aObject;
  },

  /**
   * Defines a getter property on the given object for each of the given
   * global names as accepted by Cu.importGlobalProperties. These
   * properties are imported into the shared system global, and then
   * copied onto the given object, no matter which global the object
   * belongs to.
   *
   * @param {object} aObject
   *        The object on which to define the properties.
   * @param {string[]} aNames
   *        The list of global properties to define.
   */
  defineLazyGlobalGetters(aObject, aNames) {
    for (let name of aNames) {
      ChromeUtils.defineLazyGetter(aObject, name, () => {
        if (!(name in global)) {
          let importName = EXTRA_GLOBAL_NAME_TO_IMPORT_NAME[name] || name;
          // eslint-disable-next-line mozilla/reject-importGlobalProperties, no-unused-vars
          Cu.importGlobalProperties([importName]);
        }
        return global[name];
      });
    }
  },

  /**
   * Defines a getter on a specified object for a service.  The service will not
   * be obtained until first use.
   *
   * @param {object} aObject
   *        The object to define the lazy getter on.
   * @param {string} aName
   *        The name of the getter to define on aObject for the service.
   * @param {string} aContract
   *        The contract used to obtain the service.
   * @param {nsIID} aInterface
   *        The interface or name of interface to query the service to.
   */
  defineLazyServiceGetter(aObject, aName, aContract, aInterface) {
    ChromeUtils.defineLazyGetter(aObject, aName, () => {
      return Cc[aContract].getService(aInterface);
    });
  },

  /**
   * @typedef {{[key: string]: [string, nsIID]}} ServicesDetail
   *   Details of the services by name. The first item in the value array is the
   *   contract ID, the second is the nsIID for the interface of the service.
   */

  /**
   * Defines a lazy service getter on a specified object for each
   * property in the given object.
   *
   * @param {object} aObject
   *        The object to define the lazy getter on.
   * @param {ServicesDetail} aServices
   *        An object with a property for each service to be
   *        imported.
   */
  defineLazyServiceGetters(aObject, aServices) {
    for (let [name, service] of Object.entries(aServices)) {
      // Note: This is hot code, and cross-compartment array wrappers
      // are not JIT-friendly to destructuring or spread operators, so
      // we need to use indexed access instead.
      this.defineLazyServiceGetter(aObject, name, service[0], service[1]);
    }
  },

  /**
   * Defines a getter on a specified object for preference value. The
   * preference is read the first time that the property is accessed,
   * and is thereafter kept up-to-date using a preference observer.
   *
   * @param {object} aObject
   *        The object to define the lazy getter on.
   * @param {string} aName
   *        The name of the getter property to define on aObject.
   * @param {string} aPreference
   *        The name of the preference to read.
   * @param {any} aDefaultPrefValue
   *        The default value to use, if the preference is not defined.
   *        This is the default value of the pref, before applying aTransform.
   * @param {Function} aOnUpdate
   *        A function to call upon update. Receives as arguments
   *         `(aPreference, previousValue, newValue)`
   * @param {Function} aTransform
   *        An optional function to transform the value.  If provided,
   *        this function receives the new preference value as an argument
   *        and its return value is used by the getter.
   */
  defineLazyPreferenceGetter(
    aObject,
    aName,
    aPreference,
    aDefaultPrefValue = null,
    aOnUpdate = null,
    aTransform = val => val
  ) {
    if (AppConstants.DEBUG && aDefaultPrefValue !== null) {
      let prefType = Services.prefs.getPrefType(aPreference);
      if (prefType != Ci.nsIPrefBranch.PREF_INVALID) {
        // The pref may get defined after the lazy getter is called
        // at which point the code here won't know the expected type.
        let prefTypeForDefaultValue = {
          boolean: Ci.nsIPrefBranch.PREF_BOOL,
          number: Ci.nsIPrefBranch.PREF_INT,
          string: Ci.nsIPrefBranch.PREF_STRING,
        }[typeof aDefaultPrefValue];
        if (prefTypeForDefaultValue != prefType) {
          throw new Error(
            `Default value does not match preference type (Got ${prefTypeForDefaultValue}, expected ${prefType}) for ${aPreference}`
          );
        }
      }
    }

    // Note: We need to keep a reference to this observer alive as long
    // as aObject is alive. This means that all of our getters need to
    // explicitly close over the variable that holds the object, and we
    // cannot define a value in place of a getter after we read the
    // preference.
    let observer = {
      QueryInterface: XPCU_lazyPreferenceObserverQI,

      value: undefined,

      observe(subject, topic, data) {
        if (data == aPreference) {
          if (aOnUpdate) {
            let previous = this.value;

            // Fetch and cache value.
            this.value = undefined;
            let latest = lazyGetter();
            aOnUpdate(data, previous, latest);
          } else {
            // Empty cache, next call to the getter will cause refetch.
            this.value = undefined;
          }
        }
      },
    };

    let defineGetter = get => {
      Object.defineProperty(aObject, aName, {
        configurable: true,
        enumerable: true,
        get,
      });
    };

    function lazyGetter() {
      if (observer.value === undefined) {
        let prefValue;
        switch (Services.prefs.getPrefType(aPreference)) {
          case Ci.nsIPrefBranch.PREF_STRING:
            prefValue = Services.prefs.getStringPref(aPreference);
            break;

          case Ci.nsIPrefBranch.PREF_INT:
            prefValue = Services.prefs.getIntPref(aPreference);
            break;

          case Ci.nsIPrefBranch.PREF_BOOL:
            prefValue = Services.prefs.getBoolPref(aPreference);
            break;

          case Ci.nsIPrefBranch.PREF_INVALID:
            prefValue = aDefaultPrefValue;
            break;

          default:
            // This should never happen.
            throw new Error(
              `Error getting pref ${aPreference}; its value's type is ` +
                `${Services.prefs.getPrefType(aPreference)}, which I don't ` +
                `know how to handle.`
            );
        }

        observer.value = aTransform(prefValue);
      }
      return observer.value;
    }

    defineGetter(() => {
      Services.prefs.addObserver(aPreference, observer, true);

      defineGetter(lazyGetter);
      return lazyGetter();
    });
  },

  /**
   * Defines properties on the given object which lazily import
   * an ES module or run another utility getter when accessed.
   *
   * Use this version when you need to define getters on the
   * global `this`, or any other object you can't assign to::
   *
   *    XPCOMUtils.defineLazy(this, {
   *      AppConstants: "resource://gre/modules/AppConstants.sys.mjs",
   *      verticalTabs: { pref: "sidebar.verticalTabs", default: false },
   *      MIME: { service: "@mozilla.org/mime;1", iid: Ci.nsInsIMIMEService },
   *      expensiveThing: () => fetch_or_compute(),
   *    });
   *
   * Additionally, the given object is also returned, which enables
   * type-friendly composition::
   *
   *    const existing = {
   *      someProps: new Widget(),
   *    };
   *    const combined = XPCOMUtils.defineLazy(existing, {
   *      expensiveThing: () => fetch_or_compute(),
   *    });
   *
   * The `combined` variable is the same object reference as `existing`,
   * but TypeScript also knows about lazy getters defined on it.
   *
   * Since you probably don't want aliases, you can use it like this to,
   * for example, define (static) lazy getters on a class::
   *
   *    const Widget = XPCOMUtils.defineLazy(
   *      class Widget {
   *        static normalProp = 3;
   *      },
   *      {
   *        verticalTabs: { pref: "sidebar.verticalTabs", default: false },
   *      }
   *    );
   *
   * @template {LazyDefinition} const L, T
   *
   * @param {T} lazy
   * The object to define the getters on.
   *
   * @param {L} definition
   * Each key:value property defines a getter, whose type depends on the value:
   * a "resource://module" string (see ChromeUtils.defineESModuleGetters),
   * a `() => value` function (see ChromeUtils.defineLazyGetter),
   * a `{ service: "contract", iid?: nsIID }` object
   * (see XPCOMUtils.defineLazyServiceGetter), or
   * a `{ pref: "name", default?, onUpdate?, transform? }` object
   * (see XPCOMUtils.defineLazyPreferenceGetter).
   *
   * @param {ImportESModuleOptionsDictionary} [options]
   * When importing ESModules in devtools and worker contexts,
   * the third parameter is required.
   */
  defineLazy(lazy, definition, options) {
    let modules = {};

    for (let [key, val] of Object.entries(definition)) {
      if (typeof val === "string") {
        modules[key] = val;
      } else if (typeof val === "function") {
        ChromeUtils.defineLazyGetter(lazy, key, val);
      } else if ("service" in val) {
        XPCOMUtils.defineLazyServiceGetter(lazy, key, val.service, val.iid);
      } else if ("pref" in val) {
        XPCOMUtils.defineLazyPreferenceGetter(
          lazy,
          key,
          val.pref,
          val.default,
          val.onUpdate,
          val.transform
        );
      } else {
        throw new Error(`Unkown LazyDefinition for ${key}`);
      }
    }

    ChromeUtils.defineESModuleGetters(lazy, modules, options);
    return /** @type {T & DeclaredLazy<L>} */ (lazy);
  },

  /**
   * A shorthand for XPCOMUtils.defineLazy which always returns a new lazy
   * object. Use this version if you have a global `lazy` const with all the
   * getters::
   *
   *    const lazy = XPCOMUtils.declareLazy({
   *      AppConstants: "resource://gre/modules/AppConstants.sys.mjs",
   *      verticalTabs: { pref: "sidebar.verticalTabs", default: false },
   *      MIME: { service: "@mozilla.org/mime;1", iid: Ci.nsInsIMIMEService },
   *      expensiveThing: () => fetch_or_compute(),
   *    });
   *
   * @template {LazyDefinition} const L
   * @param {L} declaration
   * @param {ImportESModuleOptionsDictionary} [options]
   */
  declareLazy(declaration, options) {
    return XPCOMUtils.defineLazy({}, declaration, options);
  },

  /**
   * Defines a non-writable property on an object.
   *
   * @param {object} aObj
   *        The object to define the property on.
   *
   * @param {string} aName
   *        The name of the non-writable property to define on aObject.
   *
   * @param {any} aValue
   *        The value of the non-writable property.
   */
  defineConstant(aObj, aName, aValue) {
    Object.defineProperty(aObj, aName, {
      value: aValue,
      enumerable: true,
      writable: false,
    });
  },
};

ChromeUtils.defineLazyGetter(XPCOMUtils, "_scriptloader", () => {
  return Services.scriptloader;
});

var XPCU_lazyPreferenceObserverQI = ChromeUtils.generateQI([
  "nsIObserver",
  "nsISupportsWeakReference",
]);
