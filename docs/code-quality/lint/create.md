# Adding a New Linter to the Tree

## Linter Requirements

For a linter to be integrated into the mozilla-central tree, it needs to have:

- Any required dependencies should be installed as part of `./mach bootstrap`
- A `./mach lint` interface
- Running `./mach lint` command must pass (note, linters can be disabled for individual directories)
- Taskcluster/Treeherder integration
- In tree documentation (under `docs/code-quality/lint`) to give a basic summary, links and any other useful information
- Unit tests (under `tools/lint/test`) to make sure that the linter works as expected and we don't regress.

The review group in Phabricator is `#linter-reviewers`.

## Linter Basics

A linter is a yaml file with a `.yml` extension. Depending on how the type of linter, there may
be python code alongside the definition, pointed to by the 'payload' attribute.

Here's a trivial example:

no-eval.yml

```yaml
EvalLinter:
    description: Ensures the string eval doesn't show up.
    extensions: ['js']
    type: string
    payload: eval
```

Now `no-eval.yml` gets passed into {func}`LintRoller.read`.

## Linter Types

There are four types of linters, though more may be added in the future.

1. string - fails if substring is found
2. regex - fails if regex matches
3. external - fails if a python function returns a non-empty result list
4. structured_log - fails if a mozlog logger emits any lint_error or lint_warning log messages

As seen from the example above, string and regex linters are very easy to create, but they
should be avoided if possible. It is much better to use a context aware linter for the language you
are trying to lint. For example, use eslint to lint JavaScript files, use ruff to lint Python
files, etc.

Which brings us to the third and most interesting type of linter,
external. External linters call an arbitrary python function which is
responsible for not only running the linter, but ensuring the results
are structured properly. For example, an external type could shell out
to a 3rd party linter, collect the output and format it into a list of
{class}`Issue` objects. The signature for this python
function is `lint(files, config, **kwargs)`, where `files` is a list of
files to lint and `config` is the linter definition defined in the `.yml`
file.

Structured log linters are much like external linters, but suitable
for cases where the linter code is using mozlog and emits
`lint_error` or `lint_warning` logging messages when the lint
fails. This is recommended for writing novel gecko-specific lints. In
this case the signature for lint functions is `lint(files, config, logger,
**kwargs)`.

## Linter Definition

Each `.yml` file must have at least one linter defined in it. Here are the supported keys:

- description - A brief description of the linter's purpose (required)
- type - One of 'string', 'regex' or 'external' (required)
- payload - The actual linting logic, depends on the type (required)
- include - A list of file paths that will be considered (optional)
- exclude - A list of file paths or glob patterns that must not be matched (optional)
- extensions - A list of file extensions to be considered (optional)
- exclude_extensions - A list of file extensions to be excluded (optional)
- setup - A function that sets up external dependencies (optional)
- support-files - A list of glob patterns matching configuration files (optional)
- find-dotfiles - If set to `true`, run on dot files (.\*) (optional)
- split-dirs - The number of levels to split each directory into before dividing the paths
  into jobs. Use this for linters that lint a directory the same way as each of its entries
  (optional)
- ignore-case - If set to `true` and `type` is regex, ignore the case (optional)

Note that a linter may not have both `extensions` and `exclude_extensions` specified at the
same time.

In addition to the above, some `.yml` files correspond to a single lint rule. For these, the
following additional keys may be specified:

- message - A string to print on infraction (optional)
- hint - A string with a clue on how to fix the infraction (optional)
- rule - An id string for the lint rule (optional)
- level - The severity of the infraction, either 'error' or 'warning' (optional)

For structured_log lints the following additional keys apply:

- logger - A StructuredLog object to use for logging. If not supplied
  one will be created (optional)

## Example

Here is an example of an external linter that shells out to the Python ruff linter,
let's call the file `ruff_lint.py` ({searchfox}`in-tree version <tools/lint/python/ruff.py>`):

```python
import json
import os
import subprocess
from collections import defaultdict
from shutil import which

from mozlint import result


RUFF_NOT_FOUND = """
Could not find ruff! Install ruff and try again.
""".strip()


def lint(paths, config, **lintargs):
    binary = which("ruff")
    if not binary:
        print(RUFF_NOT_FOUND)
        return 1

    cmd = ["ruff", "check", "--force-exclude", "--format=json"] + paths
    output = subprocess.run(cmd, stdout=subprocess.PIPE, env=os.environ).output

    # all passed
    if not output:
        return []

    try:
        issues = json.loads(output)
    except json.JSONDecodeError:
        log.error(f"Could not parse output: {output}")

    results = []
    for issue in issues:
        # convert ruff's format to mozlint's format
        res = {
            "path": issue["filename"],
            "lineno": issue["location"]["row"],
            "column": issue["location"]["column"],
            "lineoffset": issue["end_location"]["row"] - issue["location"]["row"],
            "message": issue["message"],
            "rule": issue["code"],
            "level": "error",
        }

        if issue["fix"]:
            res["hint"] = issue["fix"]["message"]

        results.append(result.from_config(config, **res))

    return {"results": results, "fixed": fixed}
```

Now here is the linter definition that would call it:

```yaml
ruff:
    description: Python Linter
    include: ["."]
    extensions: ["py"]
    support-files:
        - "**/.ruff.toml"
        - "**/ruff.toml"
        - "**/pyproject.toml"
    type: external
    payload: py.ruff:lint
```

Notice the payload has two parts, delimited by ':'. The first is the module
path, which `mozlint` resolves to a file next to the linter definition and
loads from there, so it cannot collide with a module of the same name on
`sys.path`. The second is the object path within that module (e.g, the name of
a function to call). The payload's own imports still go through `sys.path`, so
consumers of `mozlint` must keep the linter directory on it. Structured log
linters use the same mechanism.

A consumer whose definitions live apart from the modules they name (such as
comm-central, whose definitions in `comm/tools/lint` wrap payloads from
`tools/lint`) can pass those extra directories as the `linter_paths` lintarg.
They are searched after the definition's own directory.

The `support-files` key is used to list configuration files or files related
to the running of the linter itself. If using `--outgoing` or `--workdir`
and one of these files was modified, the entire tree will be linted instead of
just the modified files.

## Result definition

When generating the list of results, the following values are available.

```{eval-rst}
.. csv-table::
   :header: "Name", "Description", "Optional"
   :widths: 20, 40, 10

    "linter", "Name of the linter that flagged this error", ""
    "path", "Path to the file containing the error", ""
    "message", "Text describing the error", ""
    "lineno", "Line number that contains the error", ""
    "column", "Column containing the error", ""
    "level", "Severity of the error, either 'warning' or 'error' (default 'error')", "Yes"
    "hint", "Suggestion for fixing the error", "Yes"
    "source", "Source code context of the error", "Yes"
    "rule", "Name of the rule that was violated", "Yes"
    "lineoffset", "Denotes an error spans multiple lines, of the form (<lineno offset>, <num lines>)", "Yes"
    "diff", "A diff describing the changes that need to be made to the code", "Yes"

```

## Automated testing

Every new checker must have associated tests. If your linter is `mylinter` then the
test file should be named `tools/lint/test/test_mylinter.py` and any example files
named like `tools/lint/test/files/mylinter/my-example-file`. Be sure that your test
has been added as a section `["test_mylinter.py"]` in the manifest `tools/lint/test/python.toml`.

They should be pretty easy to write as most of the work is managed by the Mozlint
framework. The key declaration is the `LINTER` variable which must match
the linker declaration.

As an example, the {searchfox}`ruff test <tools/lint/test/test_ruff.py>` looks like the following snippet:

```python
import mozunit

LINTER = "ruff"


def test_lint_ruff(lint, paths):
    results = lint(paths("bad.py"))
    assert len(results) == 2
    assert results[0].rule == "F401"
    assert results[1].rule == "E501"
    assert results[1].lineno == 5


if __name__ == "__main__":
    mozunit.main()
```

As always with tests, please make sure that enough positive and negative cases
are covered.

To run the tests:

```shell
./mach python-test --subsuite mozlint
```

To run a specific test:

```shell
./mach python-test --subsuite mozlint tools/lint/test/test_black.py
```

More tests can be {searchfox}`found in-tree <tools/lint/test>`.

## Tracking fixed issues

All the linters that provide `fix support` returns a dictionary instead of a list.

`{"results":result,"fixed":fixed}`

- results - All the linting errors it was not able to fix
- fixed - Count of fixed errors (for `fix=False` this is 0)

Some linters (example: {searchfox}`codespell <mozilla-central/rev/0379f315c75a2875d716b4f5e1a18bf27188f1e6:tools/lint/spell/__init__.py#145-163>`) might require two passes to count the number of fixed issues.
Others might just need {searchfox}`some tuning <mozilla-central/rev/0379f315c75a2875d716b4f5e1a18bf27188f1e6:tools/lint/file-whitespace/__init__.py#28,60,85,112>`.

For adding tests to check your fixed count, add a global variable `fixed = 0`
and write a function to add your test as mentioned under `Automated testing` section.

Here's an example

```python
fixed = 0


def test_lint_codespell_fix(lint, create_temp_file):
    # Typo has been fixed in the contents to avoid triggering warning
    # 'informations' ----> 'information'
    contents = """This is a file with some typos and information.
But also testing false positive like optin (because this isn't always option)
or stuff related to our coding style like:
aparent (aParent).
but detects mistakes like mozilla
""".lstrip()

    path = create_temp_file(contents, "ignore.md")
    lint([path], fix=True)

    assert fixed == 2
```

## Bootstrapping Dependencies

Many linters, especially 3rd party ones, will require a set of dependencies. It
could be as simple as installing a binary from a package manager, or as
complicated as pulling a whole graph of tools, plugins and their dependencies.

Either way, to reduce the burden on users, linters should strive to provide
automated bootstrapping of all their dependencies. To help with this,
`mozlint` allows linters to define a `setup` config, which has the same
path object format as an external payload. For example ({searchfox}`in-tree version <tools/lint/ruff.yml>`):

```yaml
ruff:
    description: Python linter
    include: ['.']
    extensions: ['py']
    type: external
    payload: py.ruff:lint
    setup: py.ruff:setup
```

The setup function takes a single argument, the root of the repository being
linted. In the case of `ruff`, it might look like:

```python
import subprocess
from shutil import which


def setup(root, **lintargs):
    # This is a simple example. Please look at the actual source for better examples.
    if not which("ruff"):
        subprocess.call(["pip", "install", "ruff"])
```

The setup function will be called implicitly before running the linter. This
means it should return fast and not produce any output if there is no setup to
be performed.

The setup functions can also be called explicitly by running `mach lint
--setup`. This will only perform setup and not perform any linting. It is
mainly useful for other tools like `mach bootstrap` to call into.

## Adding the linter to the CI

First, the job will have to be declared in Taskcluster.

This should be done in the {searchfox}`mozlint Taskcluster configuration <taskcluster/kinds/source-test/mozlint.yml>`.
You will need to define a symbol, how it is executed and on what kind of change.

For example, for ruff, the configuration is the following:

```yaml
py-ruff:
    description: run ruff over the gecko codebase
    treeherder:
        symbol: py(ruff)
    run:
        mach: lint -l ruff -f treeherder -f json:/builds/worker/mozlint.json .
    when:
        files-changed:
            - '**/*.py'
            - '**/.ruff.toml'
```

If the linter requires an external program, you will have to install it in the {searchfox}`setup script <taskcluster/docker/lint/system-setup.sh>`
and maybe install the necessary files in the {searchfox}`Docker configuration <taskcluster/docker/lint/Dockerfile>`.

:::{note}
If the defect found by the linter is minor, make sure that it is logged as
a warning by setting `{"level": "warning"}` in the
{class}`~mozlint.result.Issue`. This means the defect will not cause a
backout if landed, but will still be surfaced by reviewbot at review time,
or when using `-W/--warnings` locally.
:::
