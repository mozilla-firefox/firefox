# API references from source comments

A comment in a directory listed in `js_source_paths` has more than one reader,
and they do not agree:

-   **Descriptions render as reStructuredText.** The `[text](/path/index.md)`
    form used elsewhere comes out literally, with only the bare URL autolinked.
    Write the link as a role instead:

        :doc:`Places </browser/places/index>`

    A Markdown code span such as `` `forceForeground` `` renders as code,
    because `docs/jsdoc-plugins/convert-descriptions-to-rst.mjs` doubles its
    backticks. `|name|` is a
    substitution reference, and an undefined one fails the build rather than
    warning.
-   **A `@param` description renders as one paragraph.** sphinx-js replaces every
    newline in one with a space, so a list or a heading written inside a
    parameter's description arrives glued to the parameter above it. Structure
    belongs in the function's own description, which keeps it.
-   **Write TypeScript types as TypeScript wants them.**
    `docs/jsdoc-plugins/accept-typescript-types.mjs` passes predicates,
    generics, indexed access, tuples, arrow and `import()` types through to the
    page verbatim, though not as links.
-   **jsdoc keeps prose written after an `@type` tag**, on the same line or
    below it, but logs a warning claiming it will ignore it. Put the prose
    above the tags to keep the build log quiet.
-   **Give `@typedef` a type** -- `@typedef {object} Name` -- or jsdoc takes the
    first `{` in the prose under it as the type and logs a build ERROR.
-   **A documented default value renders**, so `[options.animate=true]` is a
    claim about the code. Write one only where the signature supplies that
    default, and describe a computed default in the prose instead, where it can
    be kept accurate.
-   **A destructured parameter's documented name is printed in front of every
    property under it**, so the name a comment invents for an options bag is
    part of the rendered API rather than a local choice. Call it `options`,
    whether the parameter is destructured or not, unless the function takes
    two bags or passes one through to another call; there, name it for the
    call it belongs to, such as `loadOptions`.
-   **`@throws` renders** as a `throws` field carrying its type. On a class,
    though, `js:autoclass` takes parameters, return values and exceptions from
    the constructor only, so those tags render nowhere from the class's own
    comment. A method that checks its own contract at runtime documents the
    check in `@throws` rather than in a parameter's description.

## Verify by reading the rendered HTML

A passing build proves little about a generated page. sphinx-js never reads
jsdoc's exit status, so a jsdoc ERROR such as an untyped `@typedef` costs
nothing visible, and `make_sphinx_js_skip_missing_objects` in `docs/conf.py`
turns a directive that names nothing into a warning. Open the page under
`<objdir>/docs/html/` and count its member anchors (`id="<Class>.<member>"`)
against the source, then spot-check a signature. An unresolvable `@see` is the
exception: it fails the build with `'any' reference target not found`.

`jsdoc_cache` is unset, so a page that disagrees with the source is never a
stale cache. A rebuild after the first full run takes under a minute.

A clean tree logs no `Invalid type expression` line, so one in the build log
comes from your change and names its source file. The likely cause is an
untyped `@typedef`, since `accept-typescript-types.mjs` rewrites only a brace
that directly follows the tag.

## Links and anchors

-   `{@link foo}` renders as the code literal ``` ``foo`` ```, not a link:
    `convert-descriptions-to-rst.mjs` rewrites it, and `[label]{@link foo}`
    becomes ``` ``label`` ```. Use a role for a link.
-   Only members get anchors, as `id="Class.member"`, which is also the form a
    `@see` tag needs. `Class#member` does not resolve. Parameters get no
    anchors, so an option key inside a parameter list cannot be linked to.

## Types jsdoc parses differently

jsdoc parses types with catharsis. `accept-typescript-types.mjs` quotes every
expression catharsis rejects on a type tag, such as `@param`, `@returns` or
`@type`, so a build logs no `Invalid type expression` line for them and a
misspelled type reaches the page as written. An expression catharsis accepts
is parsed, and can come out differently from what TypeScript reads:

-   **`?T` parses and loses its nullability.** jsdoc records `{?Console}` as
    `Console` alone, where `{Console|null}` keeps both names, and sphinx-js
    renders only those names. tsc and eslint treat the two spellings the same,
    so only the rendered page shows the difference. Write `{T|null}` in any
    file with a generated page.

## Members that render wrongly or not at all

-   **jsdoc cannot name `#`-private members.**
    `docs/jsdoc-plugins/skip-private-members.mjs` keeps the shapes it knows
    off the page. A shape it misses aborts the whole build, either with
    `More than one object matches the path suffix` (the doclet takes the
    enclosing class's name) or with a parsimonious `ParseError` on an empty
    name that mentions no file. Widen the plugin rather than the comment.
-   **Two documented doclets at one path abort the build** with
    `Your code contains multiple documented objects at each of these paths`
    (sphinx-js's `PathsTaken`) followed by the shared path. Two shapes
    produce one, and a plugin keeps each off the page:
    -   An inline cast on a member expression yields a doclet named for the
        expression, so `/** @type {X} */ (aTab.documentGlobal)` written twice
        in one file collides at `aTab.documentGlobal`.
        `docs/jsdoc-plugins/skip-type-casts.mjs` covers it.
    -   A doc comment on a function-local variable or constant, even a lone
        `@type`, yields an `<anonymous>~name` doclet, so same-named locals in
        different functions of one file collide.
        `docs/jsdoc-plugins/skip-anonymous-locals.mjs` covers it, but not a
        documented local function, an arrow function assigned to a `const`
        included, which still collides with a same-named one in the file.
-   **A `@typedef` gets no entry of its own, and a record type renders as
    `Object`.** `js:autoclass` renders members only, so a typedef's name shows
    as plain text and its `@property` rows appear nowhere. Name the keys in the
    parameter's description when a reader of the page needs them.
-   **A subclass page shows only the subclass's own members.** sphinx-js does
    not pull inherited members in, so a thin subclass of a documented base
    renders an empty class. Document the base under its own name, with a
    `redirects` entry in `docs/config.yml` from the subclass's old page, as
    `UrlbarInput` does.
-   `_`-prefixed members, and an empty constructor entry, are kept off the page
    with `:exclude-members:`.

## Shapes that need a different directive

-   **`export const Foo = new _Foo();`** renders as `class _Foo()` with
    `_Foo.method` anchors. `@alias Foo` on the class's doc comment gives the
    page the name callers use. A plain `export class Foo` needs no tag.
-   **`export const Foo = { … }`** is not a class: an undocumented object
    literal yields nothing for `js:autoclass` to find, and documenting it gives a
    heading that reads as a class with an empty constructor. Write one
    `js:autofunction` per member instead, since there is no `automodule`, or
    use a prose page with an API reference section. The anchors are
    `Foo.member` either way.
-   **An exported module function** has global scope, so `js:autofunction`
    takes `foo` or `<FileBasename>.foo`. The bare name aborts the build with
    `More than one object matches the path suffix "foo"` as soon as anything in
    `js_source_paths` has a member of that name. Qualify every directive on the
    page with the file's basename.

## Adding a file to `js_source_paths`

-   **Measure the lint work first.** The comment above `js_source_paths` asks
    that a listed path stay out of the `valid-jsdoc` and `require-jsdoc`
    sections of `eslint-rollouts.config.mjs`. A path leaves a section by being
    named in that block's `ignores`. Add both entries and run
    `./mach lint -l eslint -f json <file>`; the count follows the comments the
    file already has, mostly `@param` types they lack, and is often the bulk of
    the work.
-   List a single file rather than a directory that also holds window scripts
    or tests, whose bare globals and duplicate local names trip
    <https://github.com/pyodide/sphinx-js/issues/242>.
-   Head the generated page with the class's name, such as `## Tabbrowser`.
    The directive adds no heading, and without one the class has nothing to
    nest under in the menu. The menu follows document order, so a
    `{toctree}` placed below an in-page `##` section lands inside that section.

## Probing jsdoc directly

Print every doclet jsdoc produces for a file, which answers whether sphinx-js
sees a member at all, how jsdoc recorded its type, and whether it has a name:

    cd docs
    ../mach node ../node_modules/.bin/jsdoc -c jsdoc.json -X <path-from-docs>

`node_modules` is there once `./mach doc` has run.
