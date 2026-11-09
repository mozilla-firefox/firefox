/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { AboutStudies } = ChromeUtils.importESModule(
  "moz-src:///browser/components/nimbus/AboutStudies.sys.mjs"
);
const { HeartbeatSurvey } = ChromeUtils.importESModule(
  "moz-src:///browser/components/nimbus/HeartbeatSurvey.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

// Make sinon assertions fail in a way that mochitest understands
sinon.assert.fail = function (message) {
  ok(false, message);
};

function withPrefEnv(inPrefs) {
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
}

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
 *
 * @param {...Function} args
 *   Functions to compose from left to right.
 */
function decorate(...args) {
  const funcs = Array.from(args);
  let decorated = funcs.pop();
  const origName = decorated.name;
  funcs.reverse();
  for (const func of funcs) {
    decorated = func(decorated);
  }
  Object.defineProperty(decorated, "name", { value: origName });
  return decorated;
}

/**
 * Wrapper around add_task for declaring tests that use several with-style
 * wrappers. The last argument should be your test function; all other arguments
 * should be functions that accept a single test function argument.
 *
 * The arguments are combined using decorate and passed to add_task as a single
 * test function.
 *
 * @param {...Function} args
 *   Wrapper functions followed by the test function.
 */
function decorate_task(...args) {
  return add_task(decorate(...args));
}

let _recipeId = 1;
function recipeFactory(overrides = {}) {
  return Object.assign(
    {
      id: _recipeId++,
      arguments: overrides.arguments || {},
    },
    overrides
  );
}

class MockHeartbeat {
  constructor() {
    this.eventEmitter = new MockEventEmitter();
  }
}

class MockEventEmitter {
  constructor() {
    this.once = sinon.stub();
  }
}

function withStubbedHeartbeat() {
  return function (testFunction) {
    return async function wrappedTestFunction(args) {
      const heartbeatInstanceStub = new MockHeartbeat();
      const heartbeatClassStub = sinon.stub();
      heartbeatClassStub.returns(heartbeatInstanceStub);
      HeartbeatSurvey.overrideHeartbeatForTests(heartbeatClassStub);

      try {
        await testFunction({
          ...args,
          heartbeatClassStub,
          heartbeatInstanceStub,
        });
      } finally {
        HeartbeatSurvey.overrideHeartbeatForTests();
      }
    };
  };
}

function withClearStorage() {
  return function (testFunction) {
    return async function wrappedTestFunction(args) {
      await HeartbeatSurvey._clearAllStorage();
      try {
        await testFunction(args);
      } finally {
        await HeartbeatSurvey._clearAllStorage();
      }
    };
  };
}
