# Firefox Home (New Tab)

All files related to Firefox Home, which includes content that appears on `about:home` and
`about:newtab`, can be found in the `browser/extensions/newtab` directory.
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

To install node dependencies, run the following from the root of the Firefox repository.
(This command uses `mach` to call `npm` and `node` commands to ensure the correct versions of Node and npm are being referenced.)

```shell
./mach newtab install
```

**IMPORTANT NOTE**: If you're using Windows, you may need to run the install command differently:

```shell
(cd browser/extensions/newtab && ../../../mach npm install)
```

### Which files should you edit?

You should not make changes to `.js` or `.css` files in `browser/extensions/newtab/css` or
`browser/extensions/newtab/data` directory. Instead, you should edit the `.jsx`, `.js`, and `.scss` source files
in `browser/extensions/newtab/content-src` directory. These files will be compiled into the `.js` and `.css` files.

## Building assets and running Firefox

The build generates the bundles and CSS into the objdir, so to build assets and run Firefox, run the following from the root of the Firefox repository:

```shell
./mach build faster && ./mach run
```

Use `./mach build` instead of `./mach build faster` if you haven't built Firefox in this checkout yet, or if your change also touches compiled code.

## Bundles left in the source tree

`./mach newtab bundle` and `./mach newtab watch` write the bundles and CSS into the source tree (`data/content/*.bundle.js` and `css/`), not the objdir. While these copies exist, the build packages them in place of the ones it generates. They go stale with the next change to `content-src/`, and Firefox keeps running the old code. `git status browser/extensions/newtab` lists them as untracked files.

To go back to the copies the build generates, delete them and rebuild:

```shell
rm -r browser/extensions/newtab/data/content/*.bundle.js browser/extensions/newtab/css
touch browser/extensions/newtab/addon-jar.mn
./mach build faster
```

Touching `addon-jar.mn` makes the build look up the source of each packaged file again. Without it, the build still points at the deleted copies.

## Continuous development / debugging

For near real-time reloading, run the following commands in **two separate terminals** to automatically rebuild bundled files whenever JSX or SCSS files change. After making a change, [restart your local instance](/devtools-user/browser_console/index.md#controlling-the-browser) to apply the updates. Run the following terminal commands:

```shell
./mach newtab watch
```

```shell
./mach run
```

**IMPORTANT NOTE**: This task will add inline source maps to help with debugging, which changes the memory footprint. Do not use the `watch` task for profiling or performance testing! After finishing your work, **be sure to stop the watch process**. When stopped, it will automatically run a final bundle step to remove the temporary inline source maps. That step leaves bundles in the source tree; see "Bundles left in the source tree" above to remove them.

## Pre-commit git hook

A pre-commit hook is available in `tools/lint/hooks_newtab.py`. When installed,
it runs the following checks in order, stopping at the first failure:

1. **Bundle** — if newtab source or bundle output files are staged, runs
   `./mach newtab bundle` and blocks the commit if the output files differ
   from what is staged.
2. **Lint** — runs {doc}`mozlint </code-quality/lint/usage>`
   on staged files and blocks the commit if there are errors.
3. **Tests** — optionally runs unit tests (configurable, disabled by default).

To install the hook, run the following from the root of the Firefox repository:

```shell
ln -sf "$(git rev-parse --show-toplevel)/tools/lint/hooks_newtab.py" \
       "$(git rev-parse --show-toplevel)/.git/hooks/pre-commit"
```

By default, no tests are run on commit. To configure which tests run, use
`git config newtab.pre-commit.tests` with one of the following values:

```shell
git config newtab.pre-commit.tests jest       # Jest only (~30 seconds)
git config newtab.pre-commit.tests all        # all tests including xpcshell and browser (10+ minutes)
git config newtab.pre-commit.tests none       # skip tests (default)
```

To commit despite failing tests, use `git commit --no-verify` to bypass the hook entirely.

## Running tests

New Tab unit tests are written using [Jest](https://jestjs.io) and
[React Testing Library](https://testing-library.com/docs/react-testing-library/intro/),
and other errors that may show up there are
[SCSS](https://sass-lang.com/documentation/syntax) issues flagged by
[stylelint](https://stylelint.io). These things are all run using
`npm test` under the `node(newtab)` slug in Treeherder/Try, so if that slug turns
red, these tests are what is failing. To execute them, do this:

```shell
./mach npm test --prefix=browser/extensions/newtab
```

These tests are not currently run by `mach test`, but there's a
[task filed to fix that](https://bugzilla.mozilla.org/show_bug.cgi?id=1581165).

Windows isn't currently supported by `npm test`
([path/invocation difference](https://bugzilla.mozilla.org/show_bug.cgi?id=1737419)).
To run newtab specific tests that aren't covered by `mach lint` and
`mach test`:

```shell
./mach npm run lint:stylelint --prefix=browser/extensions/newtab
./mach npm run testmc:jest --prefix=browser/extensions/newtab
```

Mochitests and xpcshell tests run normally, using `mach test`.

## Code Coverage

CI collects Jest code coverage for the unit tests and reports it to the code
coverage tools and Searchfox. There are no coverage thresholds.

To inspect the coverage report locally, run

```shell
./mach npm run testmc:jest:coverage --prefix=browser/extensions/newtab &&
./mach npm run debugcoverage --prefix=browser/extensions/newtab
```

## Discovery Stream Developer tools

You can access the developer tools for the Discovery Stream components of <about:newtab> by
visiting `about:config` and setting `browser.newtabpage.activity-stream.asrouter.devtoolsEnabled`
to `true`.

Then, go to any `about:newtab` page and click on the wrench icon in the top-right corner.

## Detailed Docs

```{toctree}
:glob: true
:titlesonly: true

v2-system-addon/*
```

[node.js homepage]: https://nodejs.org/
