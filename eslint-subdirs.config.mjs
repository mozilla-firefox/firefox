/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * ESLint's flat configuration doesn't automatically read sub-configuration files.
 * Hence, we import them here, adjusting paths as we go.
 *
 * Over time we would like to reduce this list. Ideally, this file would not exist.
 * The aim is for our rules to be consistently applied across the code base.
 *
 * If you are seeking to add a new sub-file here, please talk to the "JavaScript
 * usage, tools, and style" team first (aka #frontend-codestyle-reviewers).
 */

async function convertConfigurationFile(directory) {
  // eslint-disable-next-line no-unsanitized/method
  let config = await import(`./${directory}/.eslintrc.mjs`);
  let sectionId = 0;
  let newConfig = [];
  for (let section of config.default) {
    let newSection = { ...section };
    newSection.name = directory + "/.eslintrc.js-" + sectionId++;

    if (!newSection.files) {
      newSection.files = [`${directory}/`];
    } else if (Array.isArray(newSection.files)) {
      newSection.files = newSection.files.map(f => `${directory}/${f}`);
    } else if (typeof newSection.files == "string") {
      newSection.files = [`${directory}/${newSection.files}`];
    } else {
      throw new Error(
        "Unexpected type for the files property in configuration for",
        directory
      );
    }
    newConfig.push(newSection);
  }
  return newConfig;
}

export default [
  ...(await convertConfigurationFile("accessible/tests/browser")),
  ...(await convertConfigurationFile("accessible/tests/mochitest")),
  ...(await convertConfigurationFile("browser")),
  ...(await convertConfigurationFile(
    "browser/base/content/test/browser-webextensions"
  )),
  ...(await convertConfigurationFile("browser/components")),
  ...(await convertConfigurationFile(
    "browser/components/aboutlogins/tests/chrome"
  )),
  ...(await convertConfigurationFile("browser/components/aboutwelcome")),
  ...(await convertConfigurationFile("browser/components/asrouter")),
  ...(await convertConfigurationFile("browser/components/customizableui")),
  ...(await convertConfigurationFile(
    "browser/components/customizableui/content"
  )),
  ...(await convertConfigurationFile(
    "browser/components/enterprisepolicies/tests/xpcshell"
  )),
  ...(await convertConfigurationFile("browser/components/extensions")),
  ...(await convertConfigurationFile("browser/components/extensions/child")),
  ...(await convertConfigurationFile("browser/components/extensions/parent")),
  ...(await convertConfigurationFile(
    "browser/components/extensions/test/browser"
  )),
  ...(await convertConfigurationFile(
    "browser/components/extensions/test/mochitest"
  )),
  ...(await convertConfigurationFile(
    "browser/components/extensions/test/xpcshell"
  )),
  ...(await convertConfigurationFile("browser/components/migration")),
  ...(await convertConfigurationFile("browser/components/pagedata")),
  ...(await convertConfigurationFile(
    "browser/components/resistfingerprinting/test/mochitest"
  )),
  ...(await convertConfigurationFile("browser/components/search")),
  ...(await convertConfigurationFile("browser/components/urlbar")),
  ...(await convertConfigurationFile("browser/extensions/newtab")),
  ...(await convertConfigurationFile(
    "browser/extensions/pictureinpicture/tests/browser"
  )),
  ...(await convertConfigurationFile(
    "browser/extensions/search-detection/tests/browser"
  )),
  ...(await convertConfigurationFile("devtools")),
  ...(await convertConfigurationFile("devtools/client")),
  ...(await convertConfigurationFile("devtools/client/debugger/src")),
  ...(await convertConfigurationFile("devtools/client/dom")),
  ...(await convertConfigurationFile("devtools/client/framework/test/reload")),
  ...(await convertConfigurationFile("devtools/client/jsonview")),
  ...(await convertConfigurationFile("devtools/client/memory")),
  ...(await convertConfigurationFile("devtools/client/netmonitor/test")),
  ...(await convertConfigurationFile("devtools/client/performance-new")),
  ...(await convertConfigurationFile("devtools/client/shared/components")),
  ...(await convertConfigurationFile("devtools/server/tests/xpcshell")),
  ...(await convertConfigurationFile("devtools/shared")),
  ...(await convertConfigurationFile("dom/base/test/jsmodules")),
  ...(await convertConfigurationFile("dom/fs/test/common")),
  ...(await convertConfigurationFile("dom/fs/test/mochitest/worker")),
  ...(await convertConfigurationFile("dom/fs/test/xpcshell/worker")),
  ...(await convertConfigurationFile("dom/media/mediasource/test")),
  ...(await convertConfigurationFile("dom/quota/test/modules/system/worker")),
  ...(await convertConfigurationFile("js/src/builtin")),
  ...(await convertConfigurationFile("mobile/android")),
  ...(await convertConfigurationFile(
    "mobile/android/android-components/components/feature/webcompat-reporter/src/main/assets/extensions/webcompat-reporter"
  )),
  ...(await convertConfigurationFile(
    "mobile/android/examples/messaging_example/app/src/main/assets/messaging"
  )),
  ...(await convertConfigurationFile(
    "mobile/android/examples/port_messaging_example/app/src/main/assets/messaging"
  )),
  ...(await convertConfigurationFile(
    "mobile/android/fenix/app/src/androidTest/java/org/mozilla/fenix/syncintegration"
  )),
  ...(await convertConfigurationFile(
    "mobile/android/geckoview/src/androidTest/assets/web_extensions"
  )),
  ...(await convertConfigurationFile("mobile/shared")),
  ...(await convertConfigurationFile("mobile/shared/components/extensions")),
  ...(await convertConfigurationFile(
    "mobile/shared/components/extensions/test/mochitest"
  )),
  ...(await convertConfigurationFile(
    "mobile/shared/components/extensions/test/xpcshell"
  )),
  ...(await convertConfigurationFile("netwerk/test/perf")),
  ...(await convertConfigurationFile("remote/marionette")),
  ...(await convertConfigurationFile("remote/marionette/test/xpcshell")),
  ...(await convertConfigurationFile("security")),
  ...(await convertConfigurationFile("security/manager/ssl/tests")),
  ...(await convertConfigurationFile("security/manager/tools")),
  ...(await convertConfigurationFile("services/sync/tests/tps")),
  ...(await convertConfigurationFile("taskcluster/docker/index-task")),
  ...(await convertConfigurationFile("taskcluster/docker/periodic-updates")),
  ...(await convertConfigurationFile(
    "testing/talos/talos/tests/perf-reftest-singletons"
  )),
  ...(await convertConfigurationFile(
    "testing/mozbase/mozprofile/tests/files/dummy-profile"
  )),
  ...(await convertConfigurationFile("testing/performance")),
  ...(await convertConfigurationFile("testing/raptor")),
  ...(await convertConfigurationFile("testing/talos")),
  ...(await convertConfigurationFile(
    "testing/talos/talos/tests/devtools/addon/content"
  )),
  ...(await convertConfigurationFile("testing/talos/talos/tests/perf-reftest")),
  ...(await convertConfigurationFile("toolkit")),
  ...(await convertConfigurationFile(
    "toolkit/components/antitracking/test/browser"
  )),
  ...(await convertConfigurationFile("toolkit/components/extensions")),
  ...(await convertConfigurationFile("toolkit/components/extensions/child")),
  ...(await convertConfigurationFile("toolkit/components/extensions/parent")),
  ...(await convertConfigurationFile(
    "toolkit/components/extensions/test/browser"
  )),
  ...(await convertConfigurationFile(
    "toolkit/components/extensions/test/mochitest"
  )),
  ...(await convertConfigurationFile(
    "toolkit/components/extensions/test/xpcshell"
  )),
  ...(await convertConfigurationFile(
    "toolkit/components/extensions/test/xpcshell/webidl-api"
  )),
  ...(await convertConfigurationFile("toolkit/components/narrate")),
  ...(await convertConfigurationFile(
    "toolkit/components/passwordmgr/test/browser"
  )),
  ...(await convertConfigurationFile(
    "toolkit/components/passwordmgr/test/mochitest"
  )),
  ...(await convertConfigurationFile("toolkit/components/prompts/test")),
  ...(await convertConfigurationFile("toolkit/components/reader")),
  ...(await convertConfigurationFile("toolkit/content")),
  ...(await convertConfigurationFile("toolkit/modules/subprocess")),
  ...(await convertConfigurationFile("toolkit/mozapps/extensions")),
  ...(await convertConfigurationFile(
    "toolkit/mozapps/extensions/test/browser"
  )),
  ...(await convertConfigurationFile(
    "toolkit/mozapps/extensions/test/xpcshell"
  )),
  ...(await convertConfigurationFile("tools/lint/eslint")),
  ...(await convertConfigurationFile("tools/tryselect/selectors/chooser")),
  ...(await convertConfigurationFile("tools/ts")),
];
