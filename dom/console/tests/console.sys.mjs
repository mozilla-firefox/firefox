/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */
export var ConsoleTest = {
  go(dumpFunction) {
    console.log("Hello world!");
    console.createInstance().log("Hello world!");

    let c = console.createInstance({
      consoleID: "wow",
      innerID: "CUSTOM INNER",
      dump: dumpFunction,
      logModule: "_PREFIX_",
    });

    c.log("Hello world!");
    c.trace("Hello world!");

    console
      .createInstance({ innerID: "LEVEL", maxLogLevel: "Off" })
      .log("Invisible!");
    console
      .createInstance({ innerID: "LEVEL", maxLogLevel: "All" })
      .log("Hello world!");
    console
      .createInstance({
        innerID: "LEVEL",
        maxLogLevelPref: "pref.test.console",
      })
      .log("Hello world!");

    this.c2 = console.createInstance({
      innerID: "NO PREF",
      maxLogLevel: "Warn",
      maxLogLevelPref: "pref.test.console.notset",
    });
    this.c2.log("Invisible!");
    this.c2.warn("Hello world!");
  },

  go2() {
    this.c2.log("Hello world!");
  },
};
