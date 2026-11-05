# Best Practices

* [Enable JSDoc Linting](#enable-jsdoc-linting)
* [Documenting Types](#documenting-types)
  * [Avoid Missing Type Annotations](#avoid-missing-type-annotations)
  * [Variables](#variables)
  * [Avoid Object Types](#avoid-object-types)
  * [Adding Types](#adding-types)
  * [Extend a Typedef with an Intersection](#extend-a-typedef-with-an-intersection)
  * [Keep Property Tags Together](#keep-property-tags-together)
  * [Import Types from Other Files](#import-types-from-other-files)
  * [Optional Values](#optional-values)
  * [XPIDL Enums](#xpidl-enums)
  * [Browser Window Values](#browser-window-values)
  * [Custom Elements](#custom-elements)
  * [Files with API Documentation](#files-with-api-documentation)
* [XPCOMUtils.declareLazy / defineLazy](#xpcomutilsdeclarelazy--definelazy)

## Enable JSDoc Linting

ESLint supports linting of JSDoc comments. Enabling the rules on your component
will help to ensure that you avoid missing or incorrect type definitions for function parameters.

You can check if your component is already covered by ensuring it is not in the
`rollout-valid-jsdoc` or `rollout-require-jsdoc` sections of the
[eslint-rollouts.config.mjs](https://searchfox.org/firefox-main/source/eslint-rollouts.config.mjs)
file in the top level of `firefox-main`.

## Documenting Types

### Avoid Missing Type Annotations

By default, if no type annotation is given and it cannot be inferred, then
TypeScript will assign that variable the `any` type. This is a special type that
skips type checking, and therefore may cause hidden failures.

For class members and functions this means adding type definitions for all the
parameters, e.g.

```js
class Foo {
  /**
   * Stores the search string. The type here could be omitted if the property is assigned in
   * the constructor of the class.
   *
   * @type {string}
   */
  #search;

  /**
   * Details about the function.
   *
   * @param {string} searchString
   *   Param documentation.
   * @param {string[]} previousResult
   *   Param documentation.
   * @param {nsIAutoCompleteObserver} listener
   *   Param documentation.
   */
  startSearch(searchString, previousResult, listener) {}
}
```

### Variables

Variable types will be inferred from the item that they are initially assigned
to. However, sometimes you may need to define the type appropriately, especially
for sets and maps.

```js
// This will be inferred as type string.
let foo = "bar";

// This needs the type defining.
/** @type {Map<string, number>} */
let baz = new Map();
baz.set("star", 1701);
```

### Avoid Object Types

Most Firefox projects turn `noImplicitAny` off (see
[The tsconfig.json File](enabling.md#the-tsconfigjson-file)). In those
projects, TypeScript treats `object` and `Object` in JSDoc as `any`. It doesn't
check anything you read through such a value. So adding `/** @type {object} */`
to a variable silences every error on it.

### Adding Types

Ideally all types should be defined. There are two ways to do this.

The first is within JSDoc comments:

```js
/**
 * @typedef {object} submissionMapEntry
 * @property {SearchEngine} engine
 *   The search engine.
 * @property {string} termsParameterName
 *   The search term parameter name.
 */

/**
 * Alternately for function parameters:
 *
 * @param {object} options
 * @param {boolean} options.option1
 *   A required boolean property within options.
 * @param {number} [options.option2]
 *   An optional number property within options.
 */
function myFunc(options) {}
```

This may then be used within the file.

The second way may be more appropriate if a type is used widely within a
component. You can create a `types/urlbarType.d.ts` file, reference it from the
`tsconfig.json` file and include a definition such as:

```js
/**
 * A structure that holds the details of commands for results.
 */
type UrlbarResultCommand = {
  /**
   * The name of the command. Must be specified unless `children` is present.
   * When a command is picked, its name will be passed as `details.selType` to
   * `onEngagement()`. The special name "separator" will create a menu separator.
   */
  name?: string;
  /**
   * An l10n object for the command's label. Must be specified unless `name`
   * is "separator".
   */
  l10n?: L10nIdArgs;
  /**
   * If specified, a submenu will be created with the given child commands.
   */
  children?: UrlbarResultCommand[];
};
```

:::{note}
If you are sharing object types outside of your component, prefer using a proper
class definition or other structure.
:::

### Extend a Typedef with an Intersection

If a typedef gives a type in its braces, such as
`@typedef {SearchDetails} AliasSearchDetails`, TypeScript ignores the
`@property` tags that follow it. The typedef becomes another name for the type
in the braces. TypeScript then reports that each added property doesn't exist on
that type.

To add properties, declare them in a second typedef and join the two with `&`:

```js
/**
 * @typedef {object} AliasDetails
 * @property {string} alias
 *   The alias the user typed.
 */

/**
 * @typedef {SearchDetails & AliasDetails} AliasSearchDetails
 */
```

### Keep Property Tags Together

Keep the `@property` tags of a typedef together. Any other tag among them makes
TypeScript drop every `@property` tag after it. This includes an `@see`
indented under a property's description.

### Import Types from Other Files

You may import types from other files to be able to reuse them.

```js
/**
 * @import { LangTags } from "./translations.d.ts"
 */

/**
 * @import { OpenedConnection } from "resource://gre/modules/Sqlite.sys.mjs"
 */
```

A `*.d.ts` file is a special type-definition file for TypeScript. These should
generally only be used for types that are internal to your component and are
not exposed outside.

### Optional Values

Most Firefox projects turn `strictNullChecks` off. In those projects,
`T|undefined` is the same type as `T`. Adding or removing `|undefined` doesn't
change which errors TypeScript reports.

It is worth keeping `|undefined` where a value can really be absent because it
informs the reader about the case. It may also be used in future if
`strictNullChecks` is turned on.

### XPIDL Enums

Refer to XPIDL enums through their interfaces, for example
`{nsILoadInfo.SchemelessInputType}`.
{searchfox}`tools/@types/generated/lib.gecko.xpcom.d.ts` also declares a name
without the dot, `nsILoadInfo_SchemelessInputType`. That name isn't global, so
TypeScript can't find it.

### Browser Window Values

Annotate a value that holds a browser window as `BrowserWindow`. `BrowserWindow`
declares the globals of `browser.xhtml`, such as `gBrowser`, with their
appropriate types. On a plain `Window`, every property that `Window` doesn't
declare is `any`, including `gBrowser`.

To use `BrowserWindow`, add
{searchfox}`tools/@types/browser-window-globals.d.ts` to `include`. A project
that includes `tools/@types/browser-window.d.ts` already has it.

`BrowserWindow` still treats an undeclared name as `any`, including a misspelled
one (see [What Might a Low Error Count Hide?](enabling.md#what-might-a-low-error-count-hide)).
If code reads a global that `BrowserWindow` lacks, add it to the
`BrowserWindow` namespace in `browser-window-globals.d.ts`. If modules that run
in the window read it as a bare global, also add a matching `declare var` line
to `browser-window.d.ts`.

### Custom Elements

TypeScript doesn't see an element class that a script loads with
`Services.scriptloader.loadSubScript`, because no module imports the script. So
each access to a member that the class defines is an error.

Convert the script to a module if you can. Other files can then import the
class's type, but TypeScript reports errors in the module itself only if the
project includes it (see
[The tsconfig.json File](enabling.md#the-tsconfigjson-file)). Until the script
is a module, declare an interface for the element in the project's shared
declarations (see
[Shared Declarations](enabling.md#shared-declarations)).
{searchfox}`browser/components/tabbrowser/types/elements.ts` is an example.

{searchfox}`tools/@types/lib.gecko.augmentations.d.ts` declares
`MozXULElement`, `MozHTMLElement`, `MozBrowser` and the `MozElements` object for
every project. `MozElements` is a constant with an inline object type, which
TypeScript can't extend from another file. Add a missing member to
`lib.gecko.augmentations.d.ts` itself.

That file affects every project, so check all of them afterwards:

```shell
./mach lint -l typescript .
```

### Files with API Documentation

The modules under `js_source_paths` in {searchfox}`docs/config.yml` have
generated pages on Firefox Source Docs. Both jsdoc and TypeScript read their
comments, and the two
disagree about some types:

* Write `{number}`, not `{integer}`. jsdoc accepts `integer`, but TypeScript
  can't find it.
* Write `{T|null}`, not `{?T}`. TypeScript treats the two the same, but the API
  page shows `{?T}` as plain `T`.

## XPCOMUtils.declareLazy / defineLazy

These are newer functions that combine the existing
[XPCOMUtils](https://searchfox.org/firefox-main/source/js/xpconnect/loader/XPCOMUtils.sys.mjs)
functions into formats that are compatible with TypeScript.

Defining lazy objects with these functions provides the appropriate information
for TypeScript to be able to know the types on the lazy objects. Otherwise the
lazy objects will be defined as `any`.

:::{note}
Use these functions in the modules a TypeScript project checks, so that
TypeScript knows the types of their lazy objects. Until the functions
[move to ChromeUtils](https://bugzilla.mozilla.org/show_bug.cgi?id=1992437),
you need to import `XPCOMUtils` for them.
:::
