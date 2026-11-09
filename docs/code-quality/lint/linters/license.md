# License

This linter verifies if a file has a known license header.

By default, Firefox uses MPL-2 license with the [appropriate headers](https://www.mozilla.org/en-US/MPL/headers/).
In some cases (third-party code), a file might have a different header file.
If this is the case, one of the significant line of the header should be listed in the list {searchfox}`of valid licenses <tools/lint/license/valid-licenses.txt>`.

## Run Locally

This mozlint linter can be run using mach:

```{eval-rst}
.. parsed-literal::

    $ mach lint --linter license <file paths>

```

## Configuration

This linter is enabled on most of the whole code base.

## Autofix

This linter provides a `--fix` option that adds the right MPL-2 header at the right place depending on the script or source language.

## Sources

- {searchfox}`Configuration (YAML) <tools/lint/license.yml>`
- {searchfox}`Source (Rust) <tools/lint/mozcheck/src/license.rs>`

## License declarations

The `license-declarations` linter is a separate check on the `LICENSES` and
`LICENSED_UNDER` declarations in `moz.build` files, the ones the SBOM and
`about:license` are generated from. It verifies that:

- every `LICENSED_UNDER` id has a matching `LICENSES` declaration somewhere in
  the tree,
- every `spdx` flag is a valid SPDX license expression,
- no `spdx` flag repeats the `origin.license`, and no `text` flag the
  `origin.license-file`, of the `moz.yaml` covering it,
- nothing still sets the `APP_LICENSE_BLOCK`, `APP_LICENSE_LIST_BLOCK` or
  `APP_LICENSE_BODY_BLOCK` defines, which `about:license` no longer reads,
- every license id is a string literal, in a `moz.build` that parses.

The missing-declaration check can only live here. A given configuration
traverses only part of the tree -- a JS shell build sees the `LICENSED_UNDER`
in `js/` but never the `LICENSES` in `toolkit/content/licenses` -- so the build
backend drops an unmatched id rather than rejecting it. The linter reads every
`moz.build`, where an id declared nowhere at all really is a typo.

A license id declared twice is not checked here: `mozbuild/licenses.py`
rejects that at build time, both within one `moz.build` and across two of them,
at the point where the two declarations can still be told apart.

It reads them as a syntax tree rather than by executing them, which is what
lets it cover the whole tree at once, and is why a computed license id is
reported instead of skipped: an id the linter cannot read is an id it cannot
check.

### Run Locally

```{eval-rst}
.. parsed-literal::

    $ mach lint --linter license <moz.build paths>

```

Both checks live in the same configuration file, so `--linter license` runs
them both; which one reports depends on the kind of file passed.

### Configuration

This linter is enabled on the whole code base. It gathers the declared set from
the entire tree on every run, so it always runs as a single job. A `moz.yaml`
passed to it, or modified, lints the `moz.build` files at or below it.

### Autofix

This linter does not provide an autofix.

### Sources

- {searchfox}`Configuration (YAML) <tools/lint/license.yml>`
- {searchfox}`Source (Python) <tools/lint/license/__init__.py>`
