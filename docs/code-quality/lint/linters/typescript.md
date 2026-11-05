# TypeScript linter

[TypeScript](https://www.typescriptlang.org/) is being used as a type checker for JavaScript.

This is a new linter that is being set up and readied for production use. Please
see [this page] before setting up new projects.

## Run Locally

The mozlint integration of TypeScript can be run using mach:

```{eval-rst}
.. parsed-literal::

    $ mach lint --linter typescript <file paths>
```

Alternatively, omit the `--linter typescript` and run all configured linters, which will include
TypeScript.

TypeScript does not support the `--fix` option.

See the [Usage guide] for more options.

## Understanding Rules and Errors

- Only some files are linted, see the {searchfox}`configuration <tools/lint/typescript.yml>` for details.

  - By design we do not lint/format reftests nor crashtests as these are specially crafted tests.

## Common Issues and How To Solve Them

### The TypeScript type information needs updating due to source code changes

See [Updating Gecko Type Definitions](../../typescript/updatingTypes.md)

### This code should neither be linted nor formatted

- If it is a third-party piece of code, please add it to {searchfox}`ThirdPartyPaths.txt <tools/rewriting/ThirdPartyPaths.txt>`.
- If it is a generated file, please add it to {searchfox}`Generated.txt <tools/rewriting/Generated.txt>`.

## Configuration

Individual components are configured via TypeScript `tsconfig.json` files in the
project directory. See [Enabling TypeScript for a Component] for how to set one
up.

### Which Projects a Run Checks

The linter checks only the projects listed in
{searchfox}`tools/lint/typescript.yml`. For each path you give it, the linter
finds the listed project that contains the path. It then checks that whole
project, not just the path. If the path is a parent directory, such as
`browser/components`, the linter checks every listed project inside it.

If you run `mach lint` from the top of the tree without a path, it checks only
the listed projects that contain files changed in your outgoing commits and
working directory. If you have no local changes, it checks nothing and reports
no problems. If one of the changed files is under `tools/@types`, it checks
every listed project. To check every listed project yourself, pass `.`:

```shell
./mach lint -l typescript .
```

:::{note}
A passing run doesn't mean the linter checked the file you changed. TypeScript
reports errors only in files that match the project's `include` list and that
its `exclude` list doesn't remove. Look at both lists before you rely on the
result.
:::

If a path is not in a listed project, the linter checks nothing and reports
success. This also happens for a directory that has a `tsconfig.json` but isn't
listed in `typescript.yml`. To check an unlisted project, run:

```shell
./mach ts check path/to/dir
```

### Files Outside the Project Directory

A project's `tsconfig.json` can include a file from another directory, such as
another component's `types/*.ts` file. The linter checks a project only when a
changed path is inside the project's directory. So when you change that file,
the linter doesn't check the projects that include it. Files under
`tools/@types` are the exception: changing one makes a run without a path check
every listed project.

The same applies when you change the types that a module exports. A caller in
another project can break, and the linter won't check that project. After
either kind of change, check every listed project with
`./mach lint -l typescript .`.

### In CI

The `typescript` task, shown as `js(TSC)` on Treeherder, runs
`./mach lint -l typescript .`, so it checks every listed project. The task runs
only when a changed file matches one of the paths that trigger it. These paths
are the `files-changed` list in
{searchfox}`taskcluster/kinds/source-test/mozlint.yml`. The list covers the
JavaScript and TypeScript files in each listed directory, every `tsconfig.json`,
the files under `tools/@types`, and the linter's own files. If a patch changes
only files outside that list, the task doesn't run, even if a project includes
one of those files.

## Sources

- {searchfox}`Configuration (YAML) <tools/lint/typescript.yml>`
- {searchfox}`Source <tools/lint/typescript/__init__.py>`

## Builders

[Mark Banner (standard8)](https://people.mozilla.org/s?query=standard8) owns
the builders. Questions can also be asked on #lint:mozilla.org on Matrix.

### TypeScript task

This task is [tier 2]. It also reports via the code review bot. Issues should
be fixed if possible. If uncertain check with the appropriate team.

For test harness issues, file bugs in Developer Infrastructure :: Lint and Formatting.

[tier 2]: https://wiki.mozilla.org/Sheriffing/Job_Visibility_Policy#Overview_of_the_Job_Visibility_Tiers
[this page]: ../../typescript/index.md
[usage guide]: ../usage.md
[enabling typescript for a component]: ../../typescript/enabling.md
