/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

onmessage = () => {
  let logConsole = console.createInstance({
    maxLogLevelPref: "browser.test.logLevel",
    maxLogLevel: "Warn",
    logModule: "testPrefix",
  });

  logConsole.warn("Test Warn");
  logConsole.info("Test Info");

  postMessage({});
};
