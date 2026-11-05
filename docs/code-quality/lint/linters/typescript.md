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
project directory.

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
