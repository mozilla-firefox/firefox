# Browser Window Scripts

Each browser window runs its front-end scripts in its own global object. This
page explains how those scripts load and how to turn one into a module.

## How the Scripts Load

{searchfox}`browser.xhtml <browser/base/content/browser.xhtml>` loads two
scripts with `<script>` tags:
{searchfox}`global-scripts.js <browser/base/content/global-scripts.js>` and
{searchfox}`browser-main.js <browser/base/content/browser-main.js>`. Other
top-level windows load `global-scripts.js` too, so put a script that only the
browser window needs in `browser-main.js`.

The rest of the window's scripts load in one of three ways. New code should
only be added as modules.

### Subscripts

Most scripts load into the window's global object with a `loadSubScript` call:

```javascript
Services.scriptloader.loadSubScript("chrome://browser/content/browser-init.js", this);
```

`global-scripts.js` and `browser-main.js` contain a series of these calls.

### Modules

A module loads with `ChromeUtils.importESModule` instead:

```javascript
ChromeUtils.importESModule("chrome://browser/content/tabbrowser/tabgroup.mjs", { global: "current" });
```

The `{ global: "current" }` option loads the module into the window's global
object (see {doc}`JS Loader APIs </jsloader/jsloader-api>`). Each window then
gets its own copy of the module, and the module can read the window's globals
by name.

### Lazily Loaded Modules

A module that defines a global for other scripts loads lazily.
{searchfox}`browser.js <browser/base/content/browser.js>` defines a getter for
the global, which imports the module the first time a script reads it:

```javascript
ChromeUtils.defineESModuleGetters(
  this,
  {
    StatusPanel: "chrome://browser/content/tabbrowser/status-panel.mjs",
  },
  { global: "current" }
);
```

## Globals Shared Between Scripts

The top-level declarations of a script loaded with `loadSubScript` become globals of the
window. Some scripts use this to share an object. For example,
{searchfox}`browser-places.js <browser/base/content/browser-places.js>`
declares `var PlacesCommandHook` for other scripts to call. A script that only
defines a custom element usually wraps its code in a `{ }` block, which keeps
its declarations off the window's global object.

ESLint finds these globals by reading the `loadSubScript` lines in
`global-scripts.js` and `browser-main.js`.
The `browser-window` environment in
{searchfox}`eslint-plugin-mozilla <tools/lint/eslint/eslint-plugin-mozilla/lib/environments/browser-window.mjs>`
matches each line with a regular expression and collects the global
declarations of each script it names.

:::{warning}
Keep each `loadSubScript` call on one line. ESLint silently skips a call that
is split over several lines, and every global of that script disappears from
the other files' scope. `global-scripts.js` and `browser-main.js` use
`// prettier-ignore` for this reason.
:::

ESLint gives the collected globals to two sets of files:

- every script loaded with `loadSubScript`, through the
  {doc}`import-browser-window-globals </code-quality/lint/linters/eslint-plugin-mozilla/rules/import-browser-window-globals>`
  rule;
- every file named in the `globals-browser-window` block of
  {searchfox}`eslint-file-globals.config.mjs`.

TypeScript reads these globals from a declarations file instead (see
[Browser Window Code](/code-quality/typescript/enabling.md#browser-window-code)).

## Turning a Script into a Module

Before you start, check whether the script declares globals outside a `{ }`
block. If other scripts use them, the module exports them and `browser.js`
loads it lazily (see [Keep the Script's Globals](#keep-the-scripts-globals)).

### Choose Where the Module Runs

#### Custom Elements

Custom elements should be defined in a per-window module (`.mjs`), imported
with `{ global: "current" }` from the line that loaded the script.
{searchfox}`tabgroup.mjs <browser/components/tabbrowser/content/tabgroup.mjs>`
and {searchfox}`tabs.mjs <browser/components/tabbrowser/content/tabs.mjs>` are
examples.

:::{note}
An element class must extend the window's own `HTMLElement` or
`MozXULElement`. A system module that declares
`class extends HTMLElement` gets the system global's `HTMLElement` instead.
`customElements.define` accepts that class, but the element never upgrades,
and calling the constructor throws `NS_ERROR_UNEXPECTED`.
:::

#### Other Code

For code that isn't an element, a system module (`.sys.mjs`) is typically best.
A process has one instance of each system module. The module takes the window
as a parameter where it needs one.

{searchfox}`Tabbrowser.sys.mjs <browser/components/tabbrowser/Tabbrowser.sys.mjs>`
is an example: `Tabbrowser.create(window)` runs for each new window.
{searchfox}`BrowserComponents.manifest <browser/components/BrowserComponents.manifest>`
registers that call under a `browser-window-` category instead of a `loadSubScript` call
(see {doc}`CategoryManagerIndirection`).

A system module has none of the window's globals, such as `gBrowser` or
`document`. Import each module it uses, and read the rest from the window it
received. If the script uses many window globals, use a per-window module
instead.

### Keep the Script's Globals

The module exports each global that other scripts use, and `browser.js`
defines a lazy getter for it with `{ global: "current" }`, as shown in
[How the Scripts Load](#how-the-scripts-load). Other scripts keep reading the
globals by name. In the same commit:

- list each global in
  {searchfox}`browser.js.globals <browser/base/content/browser.js.globals>`,
  which the
  {doc}`no-more-globals </code-quality/lint/linters/eslint-plugin-mozilla/rules/no-more-globals>`
  rule checks;
- declare each global in
  {searchfox}`browser-window-globals.d.ts <tools/@types/browser-window-globals.d.ts>`
  and {searchfox}`browser-window.d.ts <tools/@types/browser-window.d.ts>`, so
  TypeScript sees it;
- remove the script's entry from the `globals-browser-window` block.

{searchfox}`status-panel.mjs <browser/components/tabbrowser/content/status-panel.mjs>`
is an example.

### Keep the File's History

If the script has no `{ }` block, its indentation doesn't change, so you can
rename it and turn it into a module in one commit (see
[Commit 2: Turn the Script into a Module](#commit-2-turn-the-script-into-a-module)).

Otherwise, removing the block changes the indentation of every line. Git then
doesn't recognize the file as renamed, so `git log --follow` and `git blame`
won't show the history. To keep it, land the conversion as two commits, one
that renames the file and one that turns it into a module. A follow-up patch
then tells `git blame` to skip the second commit.

#### Commit 1: Rename the Script

Rename the script with this command:

```shell
git mv path/to/script.js path/to/script.mjs
```

Update its `loadSubScript` line, its `jar.mn` entry and any
`globals-browser-window` entry. `loadSubScript` ignores the file extension, so
the script still loads as before.

If the script's file name is in the `MAPPINGS` table of the `browser-window`
environment, update that entry too, along with the script's entry in the
`support-files` list of {searchfox}`tools/lint/eslint.yml`. ESLint looks up a
script in `MAPPINGS` by its `.js` name. Without the update, ESLint can't find the
renamed script and fails on every browser-window file.

The `.mjs` extension turns on `no-shadow` with `builtinGlobals`. So a local
variable named after a DOM global, such as `name` or `parent`, becomes an
error. Rename those variables in this commit, and lint it before you go on:

```shell
./mach lint path/to/script.mjs
```

#### Commit 2: Turn the Script into a Module

Remove the `{ }` block if there is one, and add `export` to the class or the
shared globals. Replace the `loadSubScript` line with
`ChromeUtils.importESModule`, or with a lazy getter in `browser.js` for a
script with shared globals. Remove any `MAPPINGS` and `support-files` entries
for the script, since ESLint no longer reads its globals. In the same commit,
give the module its lint globals (see
[Give the Module Its Lint Globals](#give-the-module-its-lint-globals)) and
update the module paths (see [Update the Module Paths](#update-the-module-paths)).
Ask reviewers to read this commit with `git diff -w`.

These changes can't be split. `export` can't appear inside the block. Once the
block is gone, ESLint reads the file's top-level names from its `loadSubScript`
line and defines them as globals in the same file. `no-shadow` then reports
each declaration, until the switch to `importESModule` removes its
`loadSubScript` line.

#### Follow-up Patch: Ignore the Reindent

Once commit 2 has landed, add it to {searchfox}`.git-blame-ignore-revs` and
{searchfox}`.hg-annotate-ignore-revs`. The entries take the commit's landed
hashes, so commit 2 can't include them.

`git blame` then attributes the lines that commit 2 really changed, such as
the `export` lines, to earlier commits. Check with `git show -w` that those
lines are a small part of the commit before you add the entry. (In the
conversions of the tab strip, tab and tab group elements, they were 22 to 47
lines out of 1,760 to 2,976.)

### Give the Module Its Lint Globals

A per-window module imports each system module it uses, and names the window
globals it reads in a `/* global … */` comment. `tabgroup.mjs` starts like
this:

```javascript
/* global gBrowser, MozXULElement */

const { TabMetrics } = ChromeUtils.importESModule(
  "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs"
);
```

Keep system modules out of the comment. ESLint can't tell when a name in the
comment stops being a window global, but an import fails as soon as its module
is gone.

Don't add the module to the `globals-browser-window` block. That entry gives
the module every browser-window global, so each import of a system module that
the window also loads becomes a `no-shadow` error.

### Update the Module Paths

If another file imports the module's types, the module's `chrome://` URL needs
an entry in the generated type definitions (see
[Updating Gecko Type Definitions](/code-quality/typescript/index.md#updating-gecko-type-definitions)).
Run this command to regenerate them:

```shell
./mach ts update
```

TypeScript checks the module only if the `tsconfig.json` of its directory
includes it (see
[Custom Elements](/code-quality/typescript/bestPractices.md#custom-elements)).

## Getting Help

If you get stuck, ask in the Firefox Frontend channel (#frontend:mozilla.org) on
Matrix.
