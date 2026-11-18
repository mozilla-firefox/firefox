# Firefox Welcome Experience (<about:welcome>)

Files related to the Firefox welcome experience, which includes content that appears on `about:welcome`, can be found in the `browser/components/aboutwelcome` directory.
Some of these source files (such as `.js`, `.jsx`, and `.scss`) require an additional build step.
We are working on migrating this to work with `mach`, but in the meantime, please
follow the following steps if you need to make changes in this directory:

## For `.sys.mjs` files (system modules)

No build step is necessary. Use `mach` and run mochitests according to your regular Firefox workflow.

## For `.js`, `.jsx`, `.scss`, or `.css` files

### Prerequisites

You will need the following:

- Node.js 10+ (On Mac, the best way to install Node.js is to use the install link on the [Node.js homepage])
- npm (packaged with Node.js)

To install dependencies, run the following from the root of the Firefox repository.
(Using `mach` to call `npm` and `node` commands will ensure you're using the correct versions of Node and npm.)

```shell
(cd browser/components/aboutwelcome && ../../../mach npm install)
```

### Which files should you edit?

You should not make changes to `.js` or `.css` files in the `browser/components/aboutwelcome/content` directory. Instead, you should edit the `.jsx`, `.js`, and `.scss` source files
in `browser/components/aboutwelcome/content-src` directory. These files will be compiled into the `.js` and `.css` files.

## Building assets and running Firefox

To build assets and run Firefox, run the following from the root of the Firefox repository:

```shell
./mach npm run bundle --prefix=browser/components/aboutwelcome && ./mach build && ./mach run
```

## Continuous development / debugging

Running `./mach npm run watchmc --prefix=browser/components/aboutwelcome` will start a process that watches files in
`aboutwelcome` and rebuilds the bundled files when JS or CSS files change.

**IMPORTANT NOTE**: This task will add inline source maps to help with debugging, which changes the memory footprint.
Do not use the `watchmc` task for profiling or performance testing!

## Running tests

<About:welcome> unit tests are written using
[mocha](https://mochajs.org), and other errors that may show up there are
[SCSS](https://sass-lang.com/documentation/syntax) issues flagged by
[stylelint](https://stylelint.io). These things are all run using
`npm test` under the `aboutwelcome` slug in Treeherder/Try, so if that slug turns
red, these tests are what is failing. To execute them, do this:

```shell
./mach npm test --prefix=browser/components/aboutwelcome
```

Windows isn't currently supported by `npm test`
([path/invocation difference](https://bugzilla.mozilla.org/show_bug.cgi?id=1737419)).
To run tests that aren't covered by `mach lint` and
`mach test`:

```shell
./mach npm run lint:stylelint --prefix=browser/components/aboutwelcome
./mach npm run testmc:build --prefix=browser/components/aboutwelcome
./mach npm run testmc:unit --prefix=browser/components/aboutwelcome
```

Mochitests run normally, using `mach test browser/components/aboutwelcome`.

## Code Coverage

Our testing setup will run code coverage tools in addition to just the unit
tests. It will error out if the code coverage metrics don't meet certain thresholds.

If you see any missing test coverage, you can inspect the coverage report by
running

```shell
./mach npm test --prefix=browser/components/aboutwelcome &&
./mach npm run debugcoverage --prefix=browser/components/aboutwelcome
```

[node.js homepage]: https://nodejs.org/
