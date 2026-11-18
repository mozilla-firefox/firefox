# Running a performance test

## Running locally

Running a test is as simple as calling it using `mach perftest` in a Firefox source
checkout:

```
$ ./mach perftest
```

The `mach` command will bootstrap the installation of all required tools for the
framework to run, and display a selection screen to pick a test. Once the
selection is done, the performance test will run locally.

If you know what test you want to run, you can use its path explicitly:

```
$ ./mach perftest perftest_script.js
```

`mach perftest` comes with numerous options, and the test script should provide
decent defaults, so you don't need to adjust them. If you need to tweak some
options, you can use `./mach perftest --help` to learn about them.

## Running in the CI

:::{warning}
If you are looking for how to run performance tests in CI and ended up here,
you should check out {doc}`mach-try-perf`.
:::
