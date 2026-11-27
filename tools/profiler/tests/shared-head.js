/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* globals Assert */
/* globals info */

/**
 * This file contains utilities that can be shared between xpcshell tests and mochitests.
 */

const { ProfilerTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/ProfilerTestUtils.sys.mjs"
);

/**
 * This is a helper function be able to run `await wait(500)`. Unfortunately
 * this is needed as the act of collecting functions relies on the periodic
 * sampling of the threads. See:
 * https://bugzilla.mozilla.org/show_bug.cgi?id=1529053
 *
 * @param {number} time
 * @returns {Promise}
 */
async function wait(time) {
  return new Promise(resolve => {
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    setTimeout(resolve, time);
  });
}

/**
 * This escapes all characters that have a special meaning in RegExps.
 * This was stolen from https://github.com/sindresorhus/escape-string-regexp and
 * so it is licence MIT and:
 * Copyright (c) Sindre Sorhus <sindresorhus@gmail.com> (https://sindresorhus.com).
 * See the full license in https://raw.githubusercontent.com/sindresorhus/escape-string-regexp/main/license.
 *
 * @param {string} string The string to be escaped
 * @returns {string} The result
 */
function escapeStringRegexp(string) {
  if (typeof string !== "string") {
    throw new TypeError("Expected a string");
  }

  // Escape characters with special meaning either inside or outside character
  // sets.  Use a simple backslash escape when it’s always valid, and a `\xnn`
  // escape when the simpler form would be disallowed by Unicode patterns’
  // stricter grammar.
  return string.replace(/[|\\{}()[\]^$+*?.]/g, "\\$&").replace(/-/g, "\\x2d");
}

/** ------ Utility functions and definitions for raising POSIX signals ------ */

ChromeUtils.defineESModuleGetters(this, {
  Downloads: "resource://gre/modules/Downloads.sys.mjs",
});

// Find the path for a profile written to disk because of signals
async function getFullProfilePath(pid) {
  // Initially look for "MOZ_UPLOAD_DIR". If this exists, firefox will write the
  // profile file here, so this is where we need to look.
  let path = Services.env.get("MOZ_UPLOAD_DIR");
  if (!path) {
    path = await Downloads.getSystemDownloadsDirectory();
  }
  return PathUtils.join(path, `profile_0_${pid}.json`);
}

// Hardcode the constants SIGUSR1 and SIGUSR2.
// This is an absolutely terrible idea, as they are implementation defined!
// However, it turns out that for 99% of the platforms we care about, and for
// 99.999% of the platforms we test, these constants are, well, constant.
// Additionally, these constants are only for _testing_ the signal handling
// feature - the actual feature relies on platform specific definitions. This
//  may cause a mismatch if we test on on, say, a gnu hurd kernel, or on a
// linux kernel running on sparc, but the feature will not break - only
// the testing.
const SIGUSR1 = Services.appinfo.OS === "Darwin" ? 30 : 10;
const SIGUSR2 = Services.appinfo.OS === "Darwin" ? 31 : 12;

// Derived heavily from equivalent sandbox testing code. For more details see:
// https://searchfox.org/mozilla-central/rev/1aaacaeb4fa3aca6837ecc157e43e947229ba8ce/security/sandbox/test/browser_content_sandbox_utils.js#89
function raiseSignal(pid, sig) {
  const { ctypes } = ChromeUtils.importESModule(
    "moz-src:///toolkit/components/ctypes/ctypes.sys.mjs"
  );

  // Derived from functionality in js/src/devtools/rootAnalysis/utility.js
  function openLibrary(names) {
    for (const name of names) {
      try {
        return ctypes.open(name);
      } catch (e) {}
    }
    return undefined;
  }

  try {
    const libc = openLibrary([
      "libc.so.6",
      "libc.so",
      "libc.dylib",
      "libSystem.B.dylib",
    ]);
    if (!libc) {
      info("Failed to open any libc shared object");
      return { ok: false };
    }

    // c.f. https://man7.org/linux/man-pages/man2/kill.2.html
    // This choice of typing for `pid` is complex, and brittle, as it's platform
    // dependent. Getting it wrong can result in incoreect generation/calling of
    // the `kill` function. Unfortunately, as it's defined as `pid_t` in a
    // header, we can't easily get access to it. For now, we just use an
    // integer, and hope that the system int size aligns with the `pid_t` size.
    const kill = libc.declare(
      "kill",
      ctypes.default_abi,
      ctypes.int, // return value
      ctypes.int32_t, // pid
      ctypes.int // sig
    );

    let kres = kill(pid, sig);
    if (kres != 0) {
      info(`Kill returned a non-zero result ${kres}.`);
      return { ok: false };
    }

    libc.close();
  } catch (e) {
    info(`Exception ${e} thrown while trying to call kill`);
    return { ok: false };
  }

  return { ok: true };
}

/** ------ Assertions helper ------ */
/**
 * This assert helper function makes it easy to check a lot of properties in an
 * object. We augment Assert.sys.mjs to make it easier to use.
 */
Object.assign(Assert, {
  /*
   * It checks if the properties on the right are all present in the object on
   * the left. Note that the object might still have other properties (see
   * objectContainsOnly below if you want the stricter form).
   *
   * The basic form does basic equality on each expected property:
   *
   * Assert.objectContains(fixture, {
   *   foo: "foo",
   *   bar: 1,
   *   baz: true,
   * });
   *
   * But it also has a more powerful form with expectations. The available
   * expectations are:
   * - any(): this only checks for the existence of the property, not its value
   * - number(), string(), boolean(), bigint(), function(), symbol(), object():
   *   this checks if the value is of this type
   * - objectContains(expected): this applies Assert.objectContains()
   *   recursively on this property.
   * - stringContains(needle): this checks if the expected value is included in
   *   the property value.
   * - stringMatches(regexp): this checks if the property value matches this
   *   regexp. The regexp can be passed as a string, to be dynamically built.
   *
   * example:
   *
   * Assert.objectContains(fixture, {
   *   name: Expect.stringMatches(`Load \\d+:.*${url}`),
   *   data: Expect.objectContains({
   *     status: "STATUS_STOP",
   *     URI: Expect.stringContains("https://"),
   *     requestMethod: "GET",
   *     contentType: Expect.string(),
   *     startTime: Expect.number(),
   *     cached: Expect.boolean(),
   *   }),
   * });
   *
   * Each expectation will translate into one or more Assert call. Therefore if
   * one expectation fails, this will be clearly visible in the test output.
   *
   * Expectations can also be normal functions, for example:
   *
   * Assert.objectContains(fixture, {
   *   number: value => Assert.greater(value, 5)
   * });
   *
   * Note that you'll need to use Assert inside this function.
   */
  objectContains(object, expectedProperties) {
    // Basic tests: we don't want to run other assertions if these tests fail.
    if (typeof object !== "object") {
      this.ok(
        false,
        `The first parameter should be an object, but found: ${object}.`
      );
      return;
    }

    if (typeof expectedProperties !== "object") {
      this.ok(
        false,
        `The second parameter should be an object, but found: ${expectedProperties}.`
      );
      return;
    }

    for (const key of Object.keys(expectedProperties)) {
      const expected = expectedProperties[key];
      if (!(key in object)) {
        this.report(
          true,
          object,
          expectedProperties,
          `The object should contain the property "${key}", but it's missing.`
        );
        continue;
      }

      if (typeof expected === "function") {
        // This is a function, so let's call it.
        expected(
          object[key],
          `The object should contain the property "${key}" with an expected value and type.`
        );
      } else {
        // Otherwise, we check for equality.
        this.equal(
          object[key],
          expectedProperties[key],
          `The object should contain the property "${key}" with an expected value.`
        );
      }
    }
  },

  /**
   * This is very similar to the previous `objectContains`, but this also looks
   * at the number of the objects' properties. Thus this will fail if the
   * objects don't have the same properties exactly.
   */
  objectContainsOnly(object, expectedProperties) {
    // Basic tests: we don't want to run other assertions if these tests fail.
    if (typeof object !== "object") {
      this.ok(
        false,
        `The first parameter should be an object but found: ${object}.`
      );
      return;
    }

    if (typeof expectedProperties !== "object") {
      this.ok(
        false,
        `The second parameter should be an object but found: ${expectedProperties}.`
      );
      return;
    }

    // In objectContainsOnly, we specifically want to check if all properties
    // from the fixture object are expected.
    // We'll be failing a test only for the specific properties that weren't
    // expected, and only fail with one message, so that the test outputs aren't
    // spammed.
    const extraProperties = [];
    for (const fixtureKey of Object.keys(object)) {
      if (!(fixtureKey in expectedProperties)) {
        extraProperties.push(fixtureKey);
      }
    }

    if (extraProperties.length) {
      // Some extra properties have been found.
      this.report(
        true,
        object,
        expectedProperties,
        `These properties are present, but shouldn't: "${extraProperties.join(
          '", "'
        )}".`
      );
    }

    // Now, let's carry on the rest of our work.
    this.objectContains(object, expectedProperties);
  },
});

const Expect = {
  any:
    () =>
    () => {} /* We don't check anything more than the presence of this property. */,
};

/* These functions are part of the Assert object, and we want to reuse them. */
[
  "stringContains",
  "stringMatches",
  "objectContains",
  "objectContainsOnly",
].forEach(
  assertChecker =>
    (Expect[assertChecker] =
      expected =>
      (actual, ...moreArgs) =>
        Assert[assertChecker](actual, expected, ...moreArgs))
);

/* These functions will only check for the type. */
[
  "number",
  "string",
  "boolean",
  "bigint",
  "symbol",
  "object",
  "function",
].forEach(type => (Expect[type] = makeTypeChecker(type)));

function makeTypeChecker(type) {
  return (...unexpectedArgs) => {
    if (unexpectedArgs.length) {
      throw new Error(
        "Type checkers expectations aren't expecting any argument."
      );
    }
    return (actual, message) => {
      const isCorrect = typeof actual === type;
      Assert.report(!isCorrect, actual, type, message, "has type");
    };
  };
}
/* ------ End of assertion helper ------ */
