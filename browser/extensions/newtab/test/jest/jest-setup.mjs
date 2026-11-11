/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import "@testing-library/jest-dom"; // eslint-disable-line import-x/no-unassigned-import
import { EventEmitter } from "test/jest/test-utils";

// Firefox module-loader shims. System modules (lib/*.sys.mjs) resolve their
// dependencies through ChromeUtils/XPCOMUtils while their module body is being
// evaluated, which is before any beforeEach can run, so these have to live here
// rather than in a stubGlobals() call. As in the karma harness,
// importESModule() hands back the global object and the lazy-getter helpers
// reparent a module's `lazy` object onto it, so `lazy.Foo` reads
// `globalThis.Foo` at access time and a test overrides it via stubGlobals().
const reparentedOntoGlobal = new WeakSet();
function updateGlobalOrObject(object) {
  if (reparentedOntoGlobal.has(object)) {
    return globalThis;
  }
  if (Object.getPrototypeOf(object).constructor.name !== "Object") {
    return object;
  }
  reparentedOntoGlobal.add(object);
  Object.setPrototypeOf(object, globalThis);
  return globalThis;
}

globalThis.ChromeUtils = {
  importESModule: () => globalThis,
  defineESModuleGetters: updateGlobalOrObject,
  // Must stay lazy: modules pass getters that read globals (Services, Cc/Ci)
  // which only exist once a test has installed them. Evaluating eagerly here
  // throws at import time. Caches after the first read, as the real one does.
  defineLazyGetter: (object, name, getter) => {
    Object.defineProperty(object, name, {
      configurable: true,
      get() {
        const value = getter();
        Object.defineProperty(object, name, {
          configurable: true,
          writable: true,
          value,
        });
        return value;
      },
    });
  },
  generateQI: () => ({}),
};

// ImportHelper needs the real module, which the importESModule shim above
// never returns.
jest.mock("resource://newtab/lib/ImportHelper.sys.mjs", () => ({
  ImportHelper: { import: moduleURI => jest.requireActual(moduleURI) },
}));

globalThis.XPCOMUtils = {
  defineLazyGlobalGetters: updateGlobalOrObject,
  defineLazyServiceGetter: updateGlobalOrObject,
  defineLazyServiceGetters: updateGlobalOrObject,
  defineLazyPreferenceGetter: (object, name, pref, defaultValue = "") => {
    updateGlobalOrObject(object)[name] = defaultValue;
  },
  generateQI: () => ({}),
};

// Any interface a module asks for resolves to an empty object.
globalThis.Ci = new Proxy({}, { get: () => ({}) });

globalThis.AppConstants = {
  MOZILLA_OFFICIAL: true,
  MOZ_APP_VERSION: "69.0a1",
  NIGHTLY_BUILD: false,
  platform: "win",
};

globalThis.Preferences = class Preferences {
  constructor({ branch = "" } = {}) {
    this._branchStr = branch;
  }
};

globalThis.EventEmitter = EventEmitter;

if (!globalThis.console.createInstance) {
  globalThis.console.createInstance = () => ({
    debug: () => {},
    error: () => {},
    info: () => {},
    log: () => {},
    trace: () => {},
    warn: () => {},
  });
}

globalThis.requestIdleCallback = cb => {
  cb();
  return 0;
};
globalThis.cancelIdleCallback = () => {};

globalThis.IntersectionObserver = class {
  observe() {}
  unobserve() {}
  disconnect() {}
};

globalThis.matchMedia = () => ({
  matches: false,
  addListener: () => {},
  removeListener: () => {},
  addEventListener: () => {},
  removeEventListener: () => {},
});

// jsdom implements neither PointerEvent nor pointer capture. Tests need to be
// able to create the event; capture does nothing here.
if (!globalThis.PointerEvent) {
  globalThis.PointerEvent = class PointerEvent extends globalThis.MouseEvent {
    constructor(type, params = {}) {
      super(type, params);
      this.pointerId = params.pointerId ?? 0;
      this.pointerType = params.pointerType ?? "";
      this.isPrimary = params.isPrimary ?? false;
    }
  };
}

if (!globalThis.Element.prototype.setPointerCapture) {
  globalThis.Element.prototype.setPointerCapture = () => {};
  globalThis.Element.prototype.releasePointerCapture = () => {};
  globalThis.Element.prototype.hasPointerCapture = () => false;
}

if (globalThis.performance && !globalThis.performance.getEntriesByType) {
  Object.defineProperty(globalThis.performance, "getEntriesByType", {
    writable: true,
    value: () => [],
  });
}

// Fail any test that logs to console.error (React act() warnings, PropType
// errors, error-boundary logging, etc.). Tests that expect an error must spy
// on console.error themselves (their inner spy swallows the calls, and its
// afterEach restores before this one checks). See bug 2024720 for the earlier
// cleanup that this guard keeps from regressing.
let consoleErrorSpy;
beforeEach(() => {
  consoleErrorSpy = jest.spyOn(console, "error").mockImplementation(() => {});
});
afterEach(() => {
  const { calls } = consoleErrorSpy.mock;
  consoleErrorSpy.mockRestore();
  if (calls.length) {
    throw new Error(
      `Unexpected console.error in test (${calls.length}):\n${calls
        .map(args => args.join(" "))
        .join("\n\n")}`
    );
  }
});
