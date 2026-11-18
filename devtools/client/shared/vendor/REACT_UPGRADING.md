[//]: # (
This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
)

# Upgrading React

## Introduction

We use a version of React that has a few minor tweaks. We want to use an un-minified production version anyway so you need to build React yourself.

## First, Upgrade react-prop-types.js

You should start by upgrading our prop-types library to match the latest version of React. Please follow the instructions in `devtools/client/shared/vendor/REACT_PROP_TYPES_UPGRADING.md` before continuing.

## Getting the Source

```bash
git clone https://github.com/facebook/react.git --depth 1 --single-branch -b v16.8.6 # or the version you are targetting
cd react
```

Verify the `version` field in package.json, it is sometimes wrong and wouldn't match the git tag.

## Preparing to Build

We need to disable minification and tree shaking as they overcomplicate the upgrade process without adding any benefits.
We also need to generate an ES Module instead of commonjs/umd.
And inject a little hack to convert some module require paths.

Apply the following patch to React repo:
```diff
diff --git a/scripts/rollup/build.js b/scripts/rollup/build.js
index 44e5a65..75ac8bf 100644
--- a/scripts/rollup/build.js
+++ b/scripts/rollup/build.js
@@ -164,12 +164,13 @@ function getRollupOutputOptions(
     {},
     {
       file: outputPath,
-      format,
+      format: "es",
       globals,
-      freeze: !isProduction,
       interop: false,
       name: globalName,
       sourcemap: false,
+      treeshake: false,
+      freeze: false,
     }
   );
 }
@@ -357,8 +358,26 @@ function getPlugins(
     }),
     // We still need CommonJS for external deps like object-assign.
     commonjs(),
+    {
+    transformBundle(source) {
+        return (
+          source.replace(/['"]react['"]/g,
+                          "'resource://devtools/client/shared/vendor/react.mjs'")
+                // Revert the previous replace change made to a call `createElement('react')` in react-dom-dev.js
+                .replace(/createElement\(['"]resource:\/\/devtools\/client\/shared\/vendor\/react.mjs['"]\)/g,
+                          "createElementNS('http://www.w3.org/1999/xhtml', 'react')")
+                .replace(/['"]react-dom['"]/g,
+                          "'resource://devtools/client/shared/vendor/react-dom.mjs'")
+                .replace(/rendererPackageName:\s['"]resource:\/\/devtools\/client\/shared\/vendor\/react-dom.mjs['"]/g,
+                          "rendererPackageName: 'react-dom'")
+                // We have to match document.createElement, as well as ownerDocument.createElement:
+                .replace(/ocument\.createElement\(/g,
+                          "ocument.createElementNS('http://www.w3.org/1999/xhtml', ")
+        );
+      },
+    },
     // Apply dead code elimination and/or minification.
-    isProduction &&
+    false &&
       closure(
         Object.assign({}, closureOptions, {
           // Don't let it create global variables in the browser.
@@ -613,19 +631,7 @@ async function buildEverything() {
   for (const bundle of Bundles.bundles) {
     await createBundle(bundle, UMD_DEV);
     await createBundle(bundle, UMD_PROD);
-    await createBundle(bundle, UMD_PROFILING);
     await createBundle(bundle, NODE_DEV);
-    await createBundle(bundle, NODE_PROD);
-    await createBundle(bundle, NODE_PROFILING);
-    await createBundle(bundle, FB_WWW_DEV);
-    await createBundle(bundle, FB_WWW_PROD);
-    await createBundle(bundle, FB_WWW_PROFILING);
-    await createBundle(bundle, RN_OSS_DEV);
-    await createBundle(bundle, RN_OSS_PROD);
-    await createBundle(bundle, RN_OSS_PROFILING);
-    await createBundle(bundle, RN_FB_DEV);
-    await createBundle(bundle, RN_FB_PROD);
-    await createBundle(bundle, RN_FB_PROFILING);
   }

   await Packaging.copyAllShims();
``` 

## Building

```bash
npm install --global yarn
yarn
yarn build
```

## Edit the generated files

### Copy the Files Into your Firefox Repo

```bash
cd <react repo root>
export VENDOR_PATH=/path/to/mozilla-central/devtools/client/shared/vendor
cp build/dist/react.production.min.js $VENDOR_PATH/react.mjs
cp build/dist/react-dom.production.min.js $VENDOR_PATH/react-dom.mjs
cp build/dist/react-dom-server.browser.production.min.js $VENDOR_PATH/react-dom-server.mjs
cp build/dist/react-dom-test-utils.production.min.js $VENDOR_PATH/react-dom-test-utils.mjs
cp build/dist/react.development.js $VENDOR_PATH/react-dev.mjs
cp build/dist/react-dom.development.js $VENDOR_PATH/react-dom-dev.mjs
cp build/dist/react-dom-server.browser.development.js $VENDOR_PATH/react-dom-server-dev.mjs
cp build/dist/react-dom-test-utils.development.js $VENDOR_PATH/react-dom-test-utils-dev.mjs
cp build/dist/react-test-renderer.production.min.js $VENDOR_PATH/react-test-renderer.mjs
```

Finally, append the following piece of code at the end of `react.mjs`:
```
const Children = react.Children;

export {
  createFactory, createElement, Component, PureComponent, Children, createRef, cloneElement,
}
```
and the following lines to the end of `react-dev.mjs`:
```
// createFactory only exists on React object,
// and createElement is overloaded via createElementWithValidation on React object.
var createFactoryExport = React.createFactory;
var createElementExport = React.createElement;
var Children = React.Children;

export {
  createFactoryExport as createFactory, createElementExport as createElement, Component, PureComponent, Children, createRef, cloneElement,
}
```

From this point we will no longer need your react repository so feel free to delete it.

## Debugger

### Update React

- Open `devtools/client/debugger/package.json`
- Under `dependencies` update `react` and `react-dom` to the required version.
- Under `devDependencies` you may also need to update `enzyme`, `enzyme-adapter-react-16` and `enzyme-to-json` to versions compatible with the new react version.

### Build the debugger

#### Check your .mozconfig

- Ensure you are not in debug mode (`ac_add_options --disable-debug`).
- Ensure you are not using the debug version of react (`ac_add_options --disable-debug-js-modules`).

#### First build Firefox

```bash
cd <srcdir> # where sourcedir is the root of your Firefox repo.
./mach build
```

### Run the debugger tests

#### First run locally

```bash
node bin/try-runner.js
```

If there any failures fix them.

**NOTE: If there are any jest failures you will get better output by running the jest tests directly using:**

```bash
yarn test
```

If any tests fail then fix them.

#### Commit your changes

Use `git commit` (or `git commit --amend`) to commit your changes.

#### Push to try

Just because the tests run fine locally they may still fail on try. You should first ensure that `node bin/try-runner.js` passes on try:

```bash
cd <srcdir> # where sourcedir is the root of your Firefox repo.
`./mach try fuzzy`
```

- When the interface appears type `debugger`.
- Press `<enter>`.

Once these tests pass on try then push to try as normal e.g. `./mach try -b do -p all -u all -t all -e all`.

If try passes then go celebrate otherwise you are on your own.
