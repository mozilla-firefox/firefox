# Enabling TypeScript for a Component

TypeScript checks front-end code as JavaScript. Writing code as TypeScript
(`.ts`) in firefox-main is not supported, and not planned to be supported.

You can enable it for a directory by adding a `tsconfig.json` file and adding
the directory to the [TypeScript linter](../lint/linters/typescript.md).

## Steps

1. Add a `tsconfig.json` to the directory (see
   [The tsconfig.json File](#the-tsconfigjson-file)).
2. Check the directory, and exclude the modules that fail (see
   [Checking the Project](#checking-the-project)).
3. Register the directory with the linter and with CI (see
   [Registering with the Linter](#registering-with-the-linter)).

## The tsconfig.json File

Start from this file, and adjust the `../../../` prefixes to the depth of your
directory:

```json
{
  "include": ["*.sys.mjs", "types/*.ts"],
  "exclude": ["SomeModule.sys.mjs"],
  "extends": "../../../tools/@types/tsconfig.json",

  "compilerOptions": {
    "checkJs": true,
    "strict": true,
    "noImplicitAny": false,
    "strictNullChecks": false,

    "plugins": [
      {
        "transform": "../../../tools/ts/plugins/checkRootOnly.js",
        "transformProgram": true
      }
    ]
  }
}
```

`extends` points to {searchfox}`tools/@types/tsconfig.json`. The file provides
various set-up functionality to map module URLs to their sources and to provide
the Gecko DOM and XPCOM types.

It is suggested to keep the four compiler options as they are. Most projects
currently in the tree set them. Turning `noImplicitAny` or `strictNullChecks` on
adds a lot of errors and so may require their own work. Additionally they may be
easier to add in future, once there is more coverage and definition of types
across the tree.

The `checkRootOnly` plugin checks only the files that `include` matches and
`exclude` doesn't remove. TypeScript still reads the modules those files import,
for their types, but doesn't report errors in them. A file that starts with
`// @ts-nocheck` stays unchecked even when it is included.

### Include Patterns

`include` patterns accept `*`, `?` and `**`, but not braces, so `content/*.js`
and `content/*.mjs` are two possible entries. A pattern that matches nothing is
fine, as long as the project as a whole matches at least one file.

A non-module script, such as one that a page loads with a plain `<script>` tag,
can join the project through another `include` entry. Its top-level names don't
collide with names in the modules, because each module has its own scope.

### Browser Window Code

If modules in the directory run in the browser window, add
`../../../tools/@types/browser-window.d.ts` to `include`. The file declares the
globals that the scripts of `browser.xhtml` define.

Code that runs elsewhere but receives a browser window can annotate it as
`BrowserWindow`, which {searchfox}`tools/@types/browser-window-globals.d.ts`
declares.

## Shared Declarations

When several modules in a directory use the same types, declare them once in a
`.ts` file under `types/`, rather than importing them into each module. Every
file in the project can use the declarations in that file without importing
it. Add `types/*.ts` to `include` from the start, even before the directory
exists.

To reuse a typedef that a module already defines, alias it there:

```ts
type TabGroupStateData =
  import("../../sessionstore/TabGroupState.sys.mjs").TabGroupStateData;
```

:::{warning}
Don't add a top-level `import` or `export` to this file. Either one makes
TypeScript treat the file as a module, so its declarations stop being global
and the files that use them fail to type-check. An inline `import("...")` type,
as above, is fine.
:::

Another project that needs the same declarations can list the file in its own
`include`.

## Checking the Project

Check the directory, and add each module that fails to `exclude`:

```shell
./mach ts check path/to/dir
```

## Landing the Project

The project should only be landed when the linter passes. It is acceptable to
only have one or two modules enabled to begin with, and for follow-up
bugs/patches to be used for enabling TypeScript on more modules.

### Registering with the Linter

Add the directory to {searchfox}`tools/lint/typescript.yml` and to the
`files-changed` list of the `typescript` task in
{searchfox}`taskcluster/kinds/source-test/mozlint.yml`. Keep the two lists in
sync, as the comments in both files ask, and in alphabetical order. Each
directory takes two entries in `files-changed`:

```yaml
- 'path/to/dir/**/*.*js'
- 'path/to/dir/**/*.ts'
```

`typescript.yml` lists only directories, so add the directory in the same patch
that adds its `tsconfig.json`.

The linter runs a project whenever a changed file is inside its directory, and
then checks the whole project. TypeScript still reports errors only in the files
that `include` lists. To run the check as CI does, but scoped to your project:

```shell
./mach lint -l typescript path/to/dir
```

## What Might a Low Error Count Hide?

Even with an empty `exclude` list, TypeScript treats some values as `any`, and
it reports no errors for code that uses an `any` value. Three things cause this:

- With `noImplicitAny` off, a parameter without a JSDoc type is silently `any`.
  Documenting a module raises its error count, because each written type gives
  TypeScript something to check.
- `ChromeUtils.defineESModuleGetters(lazy, …)` makes every property of `lazy`
  `any`. `XPCOMUtils.declareLazy` gives each one the type of its module (see
  [Best Practices](bestPractices.md#xpcomutilsdeclarelazy--definelazy)).
- The generated `Window` type lets any property name through, so every property
  read off a `Window` is `any`. Prefer `BrowserWindow` for a browser window.

## Getting Help

If you get stuck, ask in the Lint & Formatting channel (#lint:mozilla.org) on
Matrix.
