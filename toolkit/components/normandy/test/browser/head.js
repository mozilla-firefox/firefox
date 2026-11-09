const { Preferences } = ChromeUtils.importESModule(
  "resource://gre/modules/Preferences.sys.mjs"
);

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

// Make sinon assertions fail in a way that mochitest understands
sinon.assert.fail = function (message) {
  ok(false, message);
};

const preferenceBranches = {
  user: Preferences,
  default: new Preferences({ defaultBranch: true }),
};

this.withMockPreferences = function () {
  return function (testFunction) {
    return async function inner(args) {
      const mockPreferences = new MockPreferences();
      try {
        await testFunction({ ...args, mockPreferences });
      } finally {
        mockPreferences.cleanup();
      }
    };
  };
};

class MockPreferences {
  constructor() {
    this.oldValues = { user: {}, default: {} };
  }

  set(name, value, branch = "user") {
    this.preserve(name, branch);
    preferenceBranches[branch].set(name, value);
  }

  preserve(name, branch) {
    if (branch !== "user" && branch !== "default") {
      throw new Error(`Unexpected branch ${branch}`);
    }
    if (!(name in this.oldValues[branch])) {
      const preferenceBranch = preferenceBranches[branch];
      let oldValue;
      let existed;
      try {
        oldValue = preferenceBranch.get(name);
        existed = preferenceBranch.has(name);
      } catch (e) {
        oldValue = null;
        existed = false;
      }
      this.oldValues[branch][name] = { oldValue, existed };
    }
  }

  cleanup() {
    for (const [branchName, values] of Object.entries(this.oldValues)) {
      const preferenceBranch = preferenceBranches[branchName];
      for (const [name, { oldValue, existed }] of Object.entries(values)) {
        const before = preferenceBranch.get(name);

        if (before === oldValue) {
          continue;
        }

        if (existed) {
          preferenceBranch.set(name, oldValue);
        } else if (branchName === "default") {
          Services.prefs.getDefaultBranch(name).deleteBranch("");
        } else {
          preferenceBranch.reset(name);
        }

        const after = preferenceBranch.get(name);
        if (before === after && before !== undefined) {
          throw new Error(
            `Couldn't reset pref "${name}" to "${oldValue}" on "${branchName}" branch ` +
              `(value stayed "${before}", did ${existed ? "" : "not "}exist)`
          );
        }
      }
    }
  }
}

this.withPrefEnv = function (inPrefs) {
  return function wrapper(testFunc) {
    return async function inner(args) {
      await SpecialPowers.pushPrefEnv(inPrefs);
      try {
        await testFunc(args);
      } finally {
        await SpecialPowers.popPrefEnv();
      }
    };
  };
};

/**
 * Combine a list of functions right to left. The rightmost function is passed
 * to the preceding function as the argument; the result of this is passed to
 * the next function until all are exhausted. For example, this:
 *
 * decorate(func1, func2, func3);
 *
 * is equivalent to this:
 *
 * func1(func2(func3));
 */
this.decorate = function (...args) {
  const funcs = Array.from(args);
  let decorated = funcs.pop();
  const origName = decorated.name;
  funcs.reverse();
  for (const func of funcs) {
    decorated = func(decorated);
  }
  Object.defineProperty(decorated, "name", { value: origName });
  return decorated;
};

/**
 * Wrapper around add_task for declaring tests that use several with-style
 * wrappers. The last argument should be your test function; all other arguments
 * should be functions that accept a single test function argument.
 *
 * The arguments are combined using decorate and passed to add_task as a single
 * test function.
 *
 * @param {[Function]} args
 * @example
 *   decorate_task(
 *     withMockPreferences(),
 *     withMockNormandyApi(),
 *     async function myTest(mockPreferences, mockApi) {
 *       // Do a test
 *     }
 *   );
 */
this.decorate_task = function (...args) {
  return add_task(decorate(...args));
};

this.withStub = function (
  object,
  method,
  { returnValue, as = `${method}Stub` } = {}
) {
  return function wrapper(testFunction) {
    return async function wrappedTestFunction(args) {
      const stub = sinon.stub(object, method);
      stub.returnValue = returnValue;
      try {
        await testFunction({ ...args, [as]: stub });
      } finally {
        stub.restore();
      }
    };
  };
};

this.withSpy = function (object, method, { as = `${method}Spy` } = {}) {
  return function wrapper(testFunction) {
    return async function wrappedTestFunction(args) {
      const spy = sinon.spy(object, method);
      try {
        await testFunction({ ...args, [as]: spy });
      } finally {
        spy.restore();
      }
    };
  };
};
