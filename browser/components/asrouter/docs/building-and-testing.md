AboutWelcome and ASRouter Build steps
======================

All files related to about:welcome are in the browser/components/aboutwelcome
directory. All files related to ASRouter are in the browser/components/asrouter
directory. Some of these source files (such as ``.jsx``, and ``.scss``)
require an additional build step. These build steps are also required when
changing Feature Callout or Spotlight related files.

For ``.sys.mjs`` files (system modules)
---------------------------------------------------

No build step is necessary. Use ``mach`` and run mochitests according to your regular Firefox workflow.

For ``.jsx``, ``.scss``, or ``.css`` files
---------------------------------------------------

Prerequisites
---------------------------------------------------

You will need the following:

- Node.js 10+ (On Mac, the best way to install Node.js is to use the install link on the Node.js homepage.
- npm (packaged with Node.js)

To install dependencies, run the following from the root of the Firefox repository.
(Using ``mach`` to call ``npm`` and ``node`` commands will ensure you're using the correct versions of Node and npm.)

```
./mach npm install && cd browser/components/aboutwelcome && ../../../mach npm install
```

or

```
./mach npm install && cd browser/components/asrouter && ../../../mach npm install
```

Which files should you edit?
-----------------------------------

You should not make changes to
`aboutwelcome.bundle.js`, `asrouter-admin.bundle.js`, `ASRouterAdmin.css` or `aboutwelcome.css` files. Instead, you should edit the ``.jsx`` and ``.scss`` source files
in `browser/components/aboutwelcome` or  `browser/components/asrouter` directories. These files will be compiled
into the bundle and css files.

Building assets and running
-----------------------------------

To build assets and run Firefox, run the following from the root of the Firefox repository:

```
  ./mach npm run bundle --prefix=browser/components/aboutwelcome && ./mach build && ./mach run
```

or

```
  ./mach npm run bundle --prefix=browser/components/asrouter && ./mach build && ./mach run
```

  Running unit tests
-------------
The majority of aboutwelcome and ASRouter unit tests are written using
Mocha. To execute them, on Mac OS do this:

```
./mach npm test --prefix=browser/components/aboutwelcome
```

or

```
./mach npm test --prefix=browser/components/asrouter
```

 Windows npm does not support the $npm_package_config_mc_root package.json
 variables used in the lint scripts. Instead, you have to use this command on
 Windows:

 ```
 # where $dir is aboutwelcome or asrouter
cd browser/components/$dir && ../../../mach npm run testmc:unit
```

For linting, it's recommended to use the Prettier and Eslint extensions. You can view the
directory's recommended extensions by loading the repo in VScode and opening the
extensions tab.

Our testing setup will run code coverage tools in addition to the unit
tests. It will error out if the code coverage metrics don't meet certain thresholds.

If you see any missing test coverage, you can inspect the coverage report by
running

```
./mach npm test --prefix=browser/components/aboutwelcome && ./mach npm run debugcoverage --prefix=browser/components/aboutwelcome
```

  or

```
./mach npm test --prefix=browser/components/asrouter && ./mach npm run debugcoverage --prefix=browser/components/asrouter
```

Browser tests
-----------------------------------

These tests are used to test UI-related behaviour in Firefox for Desktop. See
[Browser chrome mochitests](/testing/browser-chrome/index.md). These can be
run individually by using

```
./mach test path/to/browser_test.js
```

To run entire directories, use

```
./mach test path/to/browser.toml
```

Other helpful commands which can be used for individual tests or entire manifests:

```
# Run in headless mode, with no visible window, making it less annoying to run in the background.
./mach test path/to/browser_test.js --headless
```

```
# Run in verify mode, stress testing the code by running the test repeatedly and in chaos mode.
# If you're struggling to locally reproduce an intermittent failure, try running verify.
./mach test path/to/browser_test.js --verify
```
