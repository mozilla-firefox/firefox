# PerfDocs

[PerfDocs] is a tool that checks to make sure all performance tests are documented in tree, and generates the performance testing documentation from those descriptions. The generated files are checked in under `testing/perfdocs/generated/` and displayed in {doc}`/testing/perfdocs/index`.

## Run Locally

PerfDocs can be run using mach to verify the documentation. It fails if a test is not documented, if a `config.yml` file is invalid, or if the generated documentation is out of date:

```{eval-rst}
.. parsed-literal::

    $ ./mach perfdocs
```

Documentation can be regenerated for performance tests by using the `--generate` flag. Commit the resulting changes in `testing/perfdocs/generated/` along with your patch:

```{eval-rst}
.. parsed-literal::

    $ ./mach perfdocs --generate

```

## Running in CI

PerfDocs is not a `mach lint` linter. It runs as the `perfdocs-verify` source-test task (`doc(perfdocs)` in Treeherder) when performance testing files change. If the generated documentation is out of date, the task fails and its `perfdocs.diff` artifact shows the changes that `./mach perfdocs --generate` would make.

## Configuration

There are no configuration options available. It scans the full source tree, looking for folders named `perfdocs`, validates their content, and regenerates the documentation (if `--generate` is provided). This has been implemented for all performance testing harnesses, and the documentation generated gets displayed in {doc}`/testing/perfdocs/index`.

In the `perfdocs` folders, there needs to be an `index.md` file and it needs to contain the string `{documentation}` in some location in the file which is where the test documentation will be placed. The folders must also have a `config.yml` file following this schema (the source of truth is `CONFIG_SCHEMA` in {searchfox}`python/mozperftest/mozperftest/perfdocs/verifier.py`):

```python
CONFIG_SCHEMA = {
    "definitions": {
        "metrics_schema": {
            "metric_name": {
                "type": "object",
                "properties": {
                    "aliases": {"type": "array", "items": {"type": "string"}},
                    "description": {"type": "string"},
                    "matcher": {"type": "string"},
                },
                "required": ["description", "aliases"],
            },
        },
    },
    "type": "object",
    "properties": {
        "name": {"type": "string"},
        "manifest": {"type": "string"},
        "static-only": {"type": "boolean"},
        "hardware-docs": {"type": "boolean"},
        "metrics": {"$ref": "#/definitions/metrics_schema"},
        "suites": {
            "type": "object",
            "properties": {
                "suite_name": {
                    "type": "object",
                    "properties": {
                        "tests": {
                            "type": "object",
                            "properties": {
                                "test_name": {"type": "string"},
                            },
                        },
                        "description": {"type": "string"},
                        "owner": {"type": "string"},
                    },
                    "required": ["description"],
                }
            },
        },
    },
    "required": ["name", "manifest", "static-only", "suites"],
}
```

Set `static-only` to `true` for folders that only contain static pages and no tests. If `hardware-docs` is `true`, the `index.md` file must also contain the string `{hardware_documentation}`, which is replaced with the documentation of the hardware used for performance testing (from {searchfox}`python/mozperftest/mozperftest/perfdocs/hardware.yml`).

Here is an example of a configuration file for the Raptor framework:

```{eval-rst}
.. parsed-literal::

    name: raptor
    manifest: testing/raptor/raptor/raptor.toml
    static-only: False
    suites:
        desktop:
            description: "Desktop tests."
            tests:
                raptor-tp6: "Raptor TP6 tests."
        mobile:
            description: "Mobile tests"
        benchmarks:
            description: "Benchmark tests."
            tests:
                wasm: "All wasm tests."
```

Metrics that produce alerts can also be documented like so:

```{eval-rst}
.. parsed-literal::

    name: raptor
    manifest: testing/raptor/raptor/raptor.toml
    static-only: False
    metrics:
        "First Paint":
            description: "The description of the metric."
            aliases:
                - fcp
                - anAliasForFCP
            # Optional regex to match the metrics found in the tests with this
            # documented metric
            matcher: f.*
    suites:
        desktop:
            description: "Desktop tests."
            tests:
                raptor-tp6: "Raptor TP6 tests."
        mobile:
            description: "Mobile tests"
        benchmarks:
            description: "Benchmark tests."
            tests:
                wasm: "All wasm tests."
```

The documented metrics must exist in the tests for the suite. If they are not, then validation will fail. The same is true if a metric in a test is not documented. Also, if `metrics` are defined, then a `metrics.md` file is expected to be found in the `perfdocs` folder for the given suite. It must contain the string `{metrics_documentation}` where the documentation should be added. The `metrics.md` is renamed `{suite-name}-metrics.md` in the generated folder, so if it needs to be linked to in the `index.md` file, it should contain a `{metrics_rst_name}` string for where the link should be added - it's expected to be found in a toctree section.

Note that there needs to be a FrameworkGatherer implemented for the framework being documented since each of them may have different ways of parsing test manifests for the tests. See {searchfox}`RaptorGatherer <python/mozperftest/mozperftest/perfdocs/framework_gatherers.py>` for an example gatherer that was implemented for Raptor.

## Sources

- {searchfox}`CI task <taskcluster/kinds/source-test/mozlint.yml>`
- {searchfox}`Source <python/mozperftest/mozperftest/perfdocs>`
