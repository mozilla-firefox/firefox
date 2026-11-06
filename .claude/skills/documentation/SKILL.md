---
name: documentation
description: Use this skill when working with Firefox documentation,
  including building documentation with `./mach doc`,
  fixing Sphinx build errors or warnings, modifying existing
  documentation, or adding new docs.
---

# Firefox documentation

## Overview

Firefox documentation is built using **Sphinx**
through the `./mach doc` command. Documentation sources are distributed
throughout the repository and are written in **Markdown (`.md`)**.

The documentation system integrates:

-   Sphinx
-   MyST parser (for Markdown support, with colon fences, definition lists, field lists, and HTML admonitions enabled)
-   custom Mozilla tooling in `tools/moztreedocs/` and `docs/`

Documentation builds into static HTML that is published to **Firefox
Source Docs**.

## Core Principles

-   Always reproduce documentation issues **locally first** using
    `./mach doc --no-serve --no-open`.
-   Treat warnings seriously; they often indicate real navigation or
    reference problems.
-   Prefer **small targeted builds** while debugging.
-   Ensure new documentation is **reachable through a toctree**.
-   Keep documentation **close to the code it describes** when possible.
-   Check **what the page claims against the code**: look up each name it
    cites with `searchfox-cli -q` and confirm the code still does what the
    text says.

## Recommended Workflow

### 1. Reproduce the issue locally

Start by building the documentation locally. Redirect output to a file
rather than piping through `tail`/`grep`, since builds can be slow:

    ./mach doc --no-serve --no-open > /tmp/doc_build.txt 2>&1

Common commands:

-   `./mach doc --no-serve --no-open` -- build entire tree
-   `./mach doc <path> --no-serve --no-open` -- build a specific component (much faster)
-   `./mach doc <path>` -- build and serve with livereload, to preview the pages locally while writing

Useful flags:

-   `--no-autodoc` -- skip Python/JS API generation, for faster builds and to
    get past a JSDoc failure in a component you are not touching (see *API
    references from source comments*)
-   `--verbose` -- run Sphinx in verbose mode for debugging
-   `--disable-warnings-check` -- ignore unexpected warnings during local development
-   `--linkcheck` -- validate all links in the documentation
-   `-j JOBS` -- control parallel build jobs (defaults to CPU count)

Documentation output is generated under:

    obj-*/docs/html/

When debugging, prefer building only the relevant component instead of
rebuilding everything -- though `./mach doc <path>` still reads the whole tree,
so grep the log for the path rather than reading it through. `--no-autodoc` and
`--disable-warnings-check` cannot be combined; mach rejects the pair.

### 2. Identify the type of Sphinx problem

Most documentation build issues fall into these categories:

-   navigation problems (toctree)
-   broken references
-   duplicate labels
-   include directive errors
-   autodoc import failures
-   uncategorized documentation (missing from `docs/config.yml`)
-   configuration issues

The build output from `./mach doc` will normally indicate the failing
file and line. If the build crashes, Sphinx writes backtraces to
`/tmp/sphinx-err-*` files.

Always start by inspecting the referenced file and directive.

## Documentation Layout

Key locations:

Main documentation root:

    docs/

Component documentation often lives near the code, for example:

    devtools/docs/
    toolkit/docs/
    browser/docs/

Important configuration files:

-   `docs/config.yml` -- categories, allowed warnings, redirects, JS source paths
-   `docs/conf.py` -- Sphinx configuration (extensions, theme, MyST settings)

Custom Mozilla Sphinx integration:

    tools/moztreedocs/

### How documentation is discovered

The build system discovers documentation directories via `SPHINX_TREES`
variables in `moz.build` files. When adding documentation in a new
location, you must add a `SPHINX_TREES` entry in the relevant `moz.build`.

## Configuration: `docs/config.yml`

This file controls several critical aspects of the documentation build:

-   **`categories`**: Every documentation *tree* -- a `SPHINX_TREES` root, not
    a page -- must be assigned to a category, or a full build fails with an
    "Uncategorized documentation" error. A page added inside a tree that is
    already listed needs no entry, and the check runs only when the whole tree
    is built.
-   **`allowed_warnings`**: Regex patterns for known/acceptable Sphinx
    warnings. Warnings matching these patterns are logged as "KNOWN"
    instead of causing build failures. Scope a new entry to the path it is
    about: a class-wide entry silences that class tree-wide and for good, and
    the build still exits 0 with every match counted under `Known Failures`.
    One such line was hiding 216 broken links.
-   **`redirects`**: URL redirects for backward compatibility when
    documentation moves. Format: `old/path: new/path`.
-   **`js_source_paths`**: Directories where JSDoc generation is enabled
    (tree-wide JSDoc does not work).

## Adding New Documentation

Typical process:

1.  Create a `.md` file in the appropriate directory.
2.  Add the document to a parent `toctree`.
3.  Add the documentation path to the appropriate category in `docs/config.yml`.
4.  If adding docs in a new directory, ensure `SPHINX_TREES` is set in the
    relevant `moz.build` file.
5.  Follow the structure used by neighboring documentation.
6.  Build locally with:

        ./mach doc <path> --no-serve --no-open

7.  Resolve warnings before landing the change.

If the page does not appear in the generated navigation, verify that it
is included in a toctree.

When moving documentation to a new URL, add an entry to the `redirects`
section of `docs/config.yml` so old links continue to work.

## Cross-references between documents

Link to the **source file**, not to the generated URL:

-   Good: `[mots](/mots/index.md)`, `[Coding style](/code-quality/coding-style/index.md)`
-   Bad: `[mots](/mots/index.html)` -- produces
    `WARNING: 'myst' cross-reference target not found: '/mots/index.html' [myst.xref_missing]`
-   Bad: a full `https://firefox-source-docs.mozilla.org/...` URL for in-tree
    documentation -- it bypasses link validation and breaks when pages move.

The path is rooted at the documentation tree (leading `/`), which is the built
tree, not the source checkout: `browser/components/urlbar/docs/index.md` is
`/browser/urlbar/index.md`. The extension must match the actual source file
(always `.md`). To link to a section, append the anchor:
`/mots/index.md#desktop-theme`.

A `{doc}` role with no link text renders the target page's *title*, so a noun
after it reads twice: ``in the {doc}`api` reference`` comes out as "in the
SessionStore API reference reference". Give the role its own text where the
sentence already names the thing.

An incremental build keeps a retitled page's old title in the `{doc}` links on
other pages. Check a retitle with
`./mach doc --no-serve --no-open --outdir <dir>` into a directory that doesn't
exist yet.

A `{doc}` target is the same path *without* the extension -- the opposite of
the markdown form -- and a directory needs its `/index` spelled out.

A `{ref}` target is an explicit label, never a page title: page titles are not
labels, so `{ref}`Address Bar`` fails with `undefined label`. Link a page with
`{doc}` or the markdown form.

Explicit labels share one namespace across the whole tree, so a short one claims
its name for every page: a `(telemetry)=` in one guide takes `{ref}`telemetry``
away from the Telemetry docs, with no warning. Prefix a label with its page's
subject, and use `{doc}` when the link means the whole page.

### Anchors

`myst_heading_anchors = 5` anchors every heading, and the slug deletes every
character outside `[a-z0-9_-]` rather than replacing it, so it is not guessable:
`## Consistent profiles.ini` gives `#consistent-profilesini`, ``### `Optional<T>` ``
gives `#optionalt`, and `#### 8-, 16-, and 32-bit Integer Types` gives
`#8--16--and-32-bit-integer-types`. Read the anchor out of the built HTML instead
of deriving it. Most of the broken anchors in the tree are someone writing the
one they expected.

-   **Where that slug is unreadable, define the anchor.** Put `(optional-t)=`
    above the heading, with a blank line after it (rumdl MD022 wants one before
    every heading; the target still attaches through it).
-   **Two checks disagree about which anchors exist.** `{ref}` reaches an
    explicit target from any page, but a cross-document `doc.md#target` is
    validated against the target page's *heading* slugs alone and warns
    `local id not found in doc` for a label defined right above the heading.
    A `path#anchor` link needs the heading slug; use `{ref}` when the readable
    name matters more.
-   **A generated id is invisible to that check too.** sphinx-js writes
    `id="BrowserTestUtils.withNewTab"`, so linking it works in a browser and
    warns anyway. Use ``{js:meth}`BrowserTestUtils.withNewTab` ``.

Pages converted from the MDN wiki (`docs/nspr/`, `devtools/docs/user/`,
`security/nss/`) carry link shapes MyST cannot resolve: bare wiki page names,
`/en-US/docs/` prefixes, percent-escaped names (`I%2FO_Types`), wiki anchors
(`#Directory_I.2FO_Functions`). Each maps to an in-tree `.md` plus a heading
slug, so match them in bulk by lowercasing both sides and dropping every
non-alphanumeric character. Where the target page was never converted, name the
thing rather than link to it, citing source with `{searchfox}`.

### `eval-rst`

An `eval-rst` block is a nested parse, so rST substitution definitions do not
survive it: `.. |icon| image::` and the `|icon|` using it can sit in the same
block and still fail with `Undefined substitution referenced`. The
`substitution` extension is off, so frontmatter `{{ name }}` is no way out
either. Write the block as a native MyST directive -- `{list-table}` and most
others work as a MyST fence with markdown content, images included.

### Generated pages

`testing/perfdocs/generated/` is committed and rewritten wholesale: edit the
owning component's `perfdocs/` source, then `./mach perfdocs --generate`, and
commit both. A page built by a `docs/_addons/` extension has to be fixed in the
extension.

## API references from source comments

A directory listed in `js_source_paths` has its JSDoc rendered as an API
reference (`js:autoclass`, `js:autofunction`), which sphinx-js generates by
running jsdoc over the source. The comment then has more than one reader, and
they do not agree: descriptions render as reStructuredText, not Markdown, and
jsdoc rejects some TypeScript type spellings that tsc accepts. Read
`references/js-api-references.md` before writing or fixing a doc comment in
one of those directories, adding a file to `js_source_paths`, or chasing a
generated page that is missing a member or aborts the build.

**A JSDoc failure anywhere aborts every build.** `conf.py` hands sphinx-js the
whole `js_source_paths` list whatever directory `./mach doc` was pointed at, so
an aborting error names a file the change never touched and scoping the build
does not avoid it. `--no-autodoc` builds the page anyway, with the generated API
pages missing and `js:autoclass` warning as an unknown directive.

## Mermaid diagrams

`sphinxcontrib.mermaid` is enabled, so a fenced `mermaid` block becomes a
diagram. Sphinx only writes the diagram source into the page and mermaid renders
it in the browser from a CDN, so **a mermaid block always builds**: `./mach doc`
succeeding says nothing about the diagram, and every failure in it is invisible
until the built page is open in a browser. A block that starts with `zenuml`
draws a ZenUML sequence diagram. `tools/moztreedocs/docs/mermaid-integration.md`
describes the directive and how a diagram renders. Read `references/mermaid.md`
before adding or changing a diagram, replacing a diagram image, or checking how
one rendered.

## Best Practices

-   Always build documentation locally before pushing.
-   For a docs-only change, name the linters that apply:
    `./mach lint -l codespell -l file-whitespace -l trojan-source <path>`.
    A bare `./mach lint <path>` exits non-zero with failures from linters that
    have nothing to check in a `.md` file, which reads as though the change
    broke something.
-   Resolve warnings before landing documentation changes.
-   Keep documentation near the code it describes when appropriate.
-   Prefer `literalinclude` for code examples instead of copying code.
-   When debugging large documentation changes, build only the affected
    component.
-   Use `--no-autodoc` for faster iteration when not working on API docs.
-   If a new warning appears that is expected/acceptable, add a pattern
    to `allowed_warnings` in `docs/config.yml`.
-   For the user to preview the rendered pages after a push, Reviewbot's
    comment on the revision links a preview of every documentation file the
    diff touches, kept for one week.
