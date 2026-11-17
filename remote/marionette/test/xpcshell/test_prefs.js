/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { Branch, EnvironmentPrefs, MarionettePrefs } =
  ChromeUtils.importESModule(
    "chrome://remote/content/marionette/prefs.sys.mjs"
  );

function reset() {
  Services.prefs.setBoolPref("test.bool", false);
  Services.prefs.setStringPref("test.string", "foo");
  Services.prefs.setIntPref("test.int", 777);
}

// Give us something to work with:
reset();

add_task(function test_Branch_get_root() {
  let root = new Branch(null);
  equal(false, root.get("test.bool"));
  equal("foo", root.get("test.string"));
  equal(777, root.get("test.int"));
  Assert.throws(() => root.get("doesnotexist"), /TypeError/);
});

add_task(function test_Branch_get_branch() {
  let test = new Branch("test.");
  equal(false, test.get("bool"));
  equal("foo", test.get("string"));
  equal(777, test.get("int"));
  Assert.throws(() => test.get("doesnotexist"), /TypeError/);
});

add_task(function test_Branch_set_root() {
  let root = new Branch(null);

  try {
    root.set("test.string", "bar");
    root.set("test.in", 777);
    root.set("test.bool", true);

    equal("bar", Services.prefs.getStringPref("test.string"));
    equal(777, Services.prefs.getIntPref("test.int"));
    equal(true, Services.prefs.getBoolPref("test.bool"));
  } finally {
    reset();
  }
});

add_task(function test_Branch_set_branch() {
  let test = new Branch("test.");

  try {
    test.set("string", "bar");
    test.set("int", 888);
    test.set("bool", true);

    equal("bar", Services.prefs.getStringPref("test.string"));
    equal(888, Services.prefs.getIntPref("test.int"));
    equal(true, Services.prefs.getBoolPref("test.bool"));
  } finally {
    reset();
  }
});

add_task(function test_EnvironmentPrefs_from() {
  let prefsTable = {
    "test.bool": true,
    "test.int": 888,
    "test.string": "bar",
  };
  Services.env.set("FOO", JSON.stringify(prefsTable));

  try {
    for (let [key, value] of EnvironmentPrefs.from("FOO")) {
      equal(prefsTable[key], value);
    }
  } finally {
    Services.env.set("FOO", null);
  }
});

add_task(function test_MarionettePrefs_getters() {
  equal(false, MarionettePrefs.clickToStart);
  equal(true, MarionettePrefs.navigateAfterClickEnabled);
  equal(50, MarionettePrefs.navigateAfterClickTimeout);
  equal(2828, MarionettePrefs.port);
});

add_task(function test_MarionettePrefs_setters() {
  try {
    MarionettePrefs.port = 777;
    equal(777, MarionettePrefs.port);
  } finally {
    Services.prefs.clearUserPref("marionette.port");
  }
});

add_task(function test_MarionettePrefs_navigateAfterClick() {
  try {
    Services.prefs.setBoolPref(
      "marionette.navigate-after-click.enabled",
      false
    );
    Services.prefs.setIntPref("marionette.navigate-after-click.timeout", 5000);
    equal(false, MarionettePrefs.navigateAfterClickEnabled);
    equal(5000, MarionettePrefs.navigateAfterClickTimeout);
  } finally {
    Services.prefs.clearUserPref("marionette.navigate-after-click.enabled");
    Services.prefs.clearUserPref("marionette.navigate-after-click.timeout");
  }
});
