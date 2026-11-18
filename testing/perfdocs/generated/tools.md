# Running a performance tool

You can run `perftest-tools` locally.

## Running locally

You can run `mach perftest-tools` in a Firefox source
checkout:

```
$ ./mach perftest-tools side-by-side --help
```

The `mach` command will bootstrap the installation of all required dependencies for the
side-by-side tool to run.

The following arguments are required: `-t/--test-name`, `--base-revision`, `--new-revision`,
`--base-platform`

The `--help` argument will explain more about what arguments you need to
run in order to use the tool.
