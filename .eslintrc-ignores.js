/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * Files to exclude from ESLint.
 *
 * Please DO NOT add more third party files to this file.
 * They should be added to tools/rewriting/ThirdPartyPaths.txt instead.
 *
 * Please also DO NOT add  generated files that are for some reason checked
 * into source - add them to tools/rewriting/Generated.txt instead.
 *
 * This file should only be used for exclusions where we have:
 * - preprocessed files
 * - intentionally invalid files
 * - build directories and other items that we need to ignore
 *
 * @type {string[]}
 */
module.exports = [
  // Include all js dot files.
  "!.*.js",

  // Ignore VSCode files
  ".vscode/",

  // Always ignore node_modules.
  "**/node_modules/",

  // Always ignore crashtests - specially crafted files that originally caused a
  // crash.
  "**/crashtest/",
  "**/crashtests/",
  // Also ignore reftest - specially crafted to produce expected output.
  "**/reftest/",
  "**/reftests/",
  // Don't ignore the reftest harness files.
  "!/layout/tools/reftest/",

  // Exclude expected objdirs.
  "obj*/",

  // build/ third-party code
  "build/pgo/js-input/",

  // browser/ exclusions
  "browser/app/",
  "browser/branding/**/firefox-branding.js",
  // Gzipped test file.
  "browser/base/content/test/general/gZipOfflineChild.html",
  "browser/base/content/test/urlbar/file_blank_but_not_blank.html",
  // Pre-processed template file
  "browser/components/backup/content/archive.template.html",
  // Test files that are really json not js, and don't need to be linted.
  "browser/components/sessionstore/test/unit/data/sessionstore_valid.js",
  "browser/components/sessionstore/test/unit/data/sessionstore_invalid.js",
  // This file is split into two in order to keep it as a valid json file
  // for documentation purposes (policies.json) but to be accessed by the
  // code as a .sys.mjs (schema.sys.mjs)
  "browser/components/enterprisepolicies/schemas/schema.sys.mjs",
  // generated or library files in pocket
  "browser/components/pocket/content/panels/js/tmpl.js",
  "browser/components/pocket/content/panels/js/vendor.bundle.js",
  "browser/components/pocket/content/panels/js/main.bundle.js",
  // Include the Storybook config files.
  "!browser/components/storybook/.storybook/",
  "!browser/components/storybook/.storybook/*.js",

  // Ignore webpack about:welcome files
  "browser/components/aboutwelcome/webpack.aboutwelcome.config.js",

  // The only file in browser/locales/ is pre-processed.
  "browser/locales/",
  // Generated data files
  "browser/extensions/formautofill/phonenumberutils/PhoneNumberMetaData.sys.mjs",

  // JS file using the #include pre-processor macro, leading to syntax errors.
  "browser/extensions/webcompat/run.js",

  // Ignore newtab files
  "browser/extensions/newtab/logs/",
  // JS file using the #include pre-processor macro, leading to syntax errors.
  "browser/extensions/newtab/webext-glue/builtin-newtab.js",

  // Ignore devtools debugger files which aren't intended for linting.
  "devtools/client/debugger/bin/",
  "devtools/client/debugger/configs/",
  "devtools/client/debugger/dist/",
  "devtools/client/debugger/images/",
  "devtools/client/debugger/packages/",
  "devtools/client/debugger/test/mochitest/examples/",
  "devtools/client/debugger/index.html",
  "devtools/client/debugger/webpack.config.js",

  // Ignore devtools preferences files
  "devtools/client/preferences/",

  // Ignore devtools generated code
  "devtools/client/webconsole/test/node/fixtures/stubs/*.js",
  "!devtools/client/webconsole/test/node/fixtures/stubs/index.js",
  "devtools/client/shared/source-map-loader/test/browser/fixtures/*.js",

  // Ignore devtools files testing sourcemaps / code style
  "devtools/client/framework/test/code_*",
  "devtools/client/inspector/markup/test/events_bundle.js",
  "devtools/client/netmonitor/test/xhr_bundle.js",
  "devtools/client/webconsole/test/browser/code_bundle_nosource.js",
  "devtools/client/webconsole/test/browser/code_bundle_invalidmap.js",
  "devtools/client/webconsole/test/browser/test-autocomplete-mapped.js",
  "devtools/client/webconsole/test/browser/test-autocomplete-mapped.src.js",
  "devtools/client/inspector/markup/test/shadowdom_open_debugger.min.js",
  "devtools/client/webconsole/test/browser/test-click-function-to-source*.js",
  "devtools/client/webconsole/test/browser/test-external-script-errors.js",
  "devtools/client/webconsole/test/browser/test-mangled-function.*",
  "devtools/client/webconsole/test/browser/test-message-categories-canvas-css.js",
  "devtools/client/webconsole/test/browser/test-message-categories-empty-getelementbyid.js",
  "devtools/client/webconsole/test/browser/test-sourcemap*.js",
  "devtools/server/tests/xpcshell/setBreakpoint*",
  "devtools/server/tests/xpcshell/sourcemapped.js",

  // Ignore generated code from wasm-bindgen
  "devtools/client/performance-new/shared/profiler_get_symbols.js",

  // Testing syntax error
  "devtools/client/aboutdebugging/test/browser/resources/bad-extensions/invalid-json/manifest.json",
  "devtools/client/jsonview/test/invalid_json.json",
  "devtools/client/webconsole/test/browser/test-syntaxerror-worklet.js",
  "devtools/server/tests/xpcshell/addons/invalid-extension-manifest-badjson/manifest.json",

  // devtools specific format test file
  "devtools/server/tests/xpcshell/xpcshell_debugging_script.js",
  "devtools/shared/webconsole/test/browser/data.json",

  // Generated
  "dom/canvas/test/webgl-conf/generated/",

  // Intentionally invalid/not parsable
  "dom/html/test/test_bug677658.html",
  "dom/svg/test/test_nonAnimStrings.xhtml",
  "dom/svg/test/test_SVG_namespace_ids.html",

  // Strange encodings
  "dom/base/test/file_bug687859-16.js",
  "dom/base/test/file_bug707142_bom.json",
  "dom/base/test/file_bug707142_utf-16.json",
  "dom/encoding/test/test_utf16_files.html",
  "dom/encoding/test/file_utf16_be_bom.js",
  "dom/encoding/test/file_utf16_le_bom.js",

  // Service workers fixtures which require specific resource caching.
  "dom/base/test/file_js_cache.js",
  "dom/serviceworkers/test/file_js_cache.js",

  // Intentional broken files
  "dom/base/test/file_js_cache_syntax_error.js",
  "dom/base/test/jsmodules/test_scriptNotParsedAsModule.html",
  "dom/base/test/jsmodules/test_syntaxError.html",
  "dom/base/test/jsmodules/test_syntaxErrorAsync.html",
  "dom/base/test/jsmodules/module_badSyntax.mjs",
  "dom/base/test/jsmodules/test_syntaxErrorInline.html",
  "dom/base/test/jsmodules/test_syntaxErrorInlineAsync.html",
  "dom/base/test/test_bug687859.html",
  "dom/media/webrtc/tests/mochitests/identity/idp-bad.js",
  "dom/security/test/general/file_nonscript.json",
  "dom/serviceworkers/test/file_js_cache_syntax_error.js",
  "dom/serviceworkers/test/parse_error_worker.js",
  "dom/tests/mochitest/bugs/test_bug531176.html",
  "dom/webauthn/tests/cbor.js",
  "dom/workers/test/importScripts_worker_imported3.js",
  "dom/workers/test/invalid.js",
  "dom/workers/test/threadErrors_worker1.js",

  // Tests the module loader's path handling.
  // Dynamic imports contains non-optimal paths.
  "dom/workers/test/xpcshell/data/base_uri_worker.js",
  "dom/workers/test/xpcshell/data/base_uri_module.mjs",

  // Bug 1527075: This directory is linted in github repository
  "intl/l10n/",

  // Exclude everything but self-hosted JS
  "js/examples/",
  "js/public/",
  "js/src/devtools/",
  "js/src/jit-test/",
  "js/src/tests/",
  "js/src/Y.js",

  // Changes to XPConnect tests must be carefully audited.
  "js/xpconnect/tests/mochitest/",
  "js/xpconnect/tests/unit/",

  // Fuzzing code for testing only, targeting the JS shell
  "js/src/fuzz-tests/",

  // Template file
  "mobile/android/docs/geckoview/assets/js/search-data.json",

  // Uses `//filter substitution`
  "mobile/android/app/geckoview-prefs.js",

  // Not much JS to lint and non-standard at that
  "mobile/android/installer/",
  "mobile/android/locales/",

  // Android - Web extensions: manifest.json files may be generated by the build system.
  "mobile/android/android-components/components/feature/readerview/src/main/assets/extensions/readerview/manifest.json",
  "mobile/android/android-components/samples/browser/src/main/assets/extensions/test/manifest.json",

  // Bug 1903138: remaining issues from the firefox-android migration
  "mobile/android/android-components/docs/assets/js/icon-js.js",
  "mobile/android/fenix/app/src/androidTest/assets/",
  "mobile/android/focus-android/app/src/androidTest/assets/",

  // Pre-processed/pref files
  "modules/libpref/greprefs.js",
  "modules/libpref/init/all.js",
  "modules/libpref/test/unit/*data/",
  "toolkit/components/backgroundtasks/defaults/backgroundtasks.js",
  "toolkit/components/backgroundtasks/defaults/backgroundtasks_browser.js",

  // Only contains non-standard test files.
  "python/",

  // These are (mainly) imported code that we don't want to lint to make imports easier.
  "remote/cdp/Protocol.sys.mjs",
  "remote/cdp/test/browser/chrome-remote-interface.js",
  "remote/marionette/atom.sys.mjs",

  // This file explicitly has a syntax error and cannot be parsed by eslint.
  "remote/shared/messagehandler/test/browser/resources/modules/root/invalid.sys.mjs",

  // services/ exclusions

  // Webpack-bundled library
  "services/fxaccounts/FxAccountsPairingChannel.sys.mjs",

  // Servo is imported.
  "servo/",

  // Rust/Cargo output from running `cargo` directly
  "/target/",
  "/servo/ports/geckolib/target/",
  "/dom/base/rust/target/",
  "/servo/components/style/target/",
  "/dom/webgpu/tests/cts/vendor/target/",

  // Test files that we don't want to lint (preprocessed, minified etc)
  "testing/condprofile/condprof/tests/profile",
  "testing/mozbase/mozprofile/tests/files/prefs_with_comments.js",
  "testing/mozharness/configs/test/test_malformed.json",
  "testing/talos/talos/startup_test/sessionrestore/profile/sessionstore.js",
  "testing/talos/talos/startup_test/sessionrestore/profile-manywindows/sessionstore.js",
  // Python json.
  "testing/talos/talos/unittests/test_talosconfig_browser_config.json",
  "testing/talos/talos/unittests/test_talosconfig_test_config.json",
  // Runing Talos may extract data here, see bug 1435677.
  "testing/talos/talos/tests/tp5n/",
  "testing/talos/talos/fis/tp5n/",

  // Mainly third-party related code, that shouldn't be linted.
  "testing/web-platform/",

  // toolkit/ exclusions

  // Intentionally invalid files
  "toolkit/components/workerloader/tests/moduleF-syntax-error.js",
  "toolkit/components/enterprisepolicies/tests/browser/config_broken_json.json",
  "toolkit/mozapps/extensions/test/xpcshell/data/test_AddonRepository_fail.json",

  // Built files
  "toolkit/components/pdfjs/content/build",
  "toolkit/components/pdfjs/content/web",

  // Uses preprocessing
  "toolkit/components/reader/Readerable.sys.mjs",

  // Generated & special files in cld2
  "toolkit/components/translations/cld2/",

  // Uses preprocessing
  "toolkit/mozapps/update/tests/data/xpcshellConstantsPP.js",
  "toolkit/modules/AppConstants.sys.mjs",

  // Tests of ESLint command.
  "tools/lint/test/files",

  // Uses special template formatting.
  "tools/tryselect/selectors/chooser/templates/chooser.html",

  // Ignore preprocessed *(P)refs.js files in update-packaging.
  "tools/update-packaging/**/*refs.js",

  // Ignore pre-generated webpack and typescript transpiled files for translations
  "browser/extensions/translations/extension/",

  // "scaffolding" used by uniffi which isn't valid JS in its original form.
  "toolkit/components/uniffi-bindgen-gecko-js/src/templates/js/",
  "toolkit/components/uniffi-bindgen-gecko-js/components/generated/*",

  // Test files for circular import in modules.
  "dom/base/test/jsmodules/import_circular.mjs",
  "dom/base/test/jsmodules/import_circular_1.mjs",

  // Support for Import attributes is only available in ESLint v9.19
  // https://bugzilla.mozilla.org/show_bug.cgi?id=1944290
  "netwerk/test/mochitests/test_import_json_module.mjs",
];
