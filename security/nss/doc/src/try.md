(mozilla-projects-nss-try)=

# Try Server

This page describes how to use the NSS try server. You probably want to push to `nss-try`
before pushing to the main NSS repository.

An account with
[level 1 commit access](https://www.mozilla.org/en-US/about/governance/policies/commit/access-policy/)
is required.

## Using `./mach try`

`./mach try` builds a try-syntax commit on top of your current changeset, pushes it to
`nss-try`, and then strips the temporary commit so your local history is unchanged. It reads
the list of valid platforms, tests, and tools directly from the in-tree Taskcluster
configuration under `taskcluster/`, so the choices it accepts are always in sync with CI.

```console
$ ./mach try                              # prints help, including the current set of
                                          # valid platform / test / tool tokens
$ ./mach try -- -b do -p all -u all -t all -e all
$ ./mach try -- -p linux64 -u ssl,gtest
```

The `--` separates `mach` arguments from try-syntax arguments; anything after it is passed
through as try syntax (see {ref}`try-syntax` below).

If you have not configured the `nss-try` path yet, `./mach try` will print the exact
`[paths]` entry to add to `.hg/hgrc`.

:::{note}
Uncommitted local changes will not be pushed — `./mach try` refuses to run with a dirty
working tree. Commit (or shelve) anything you want tested before invoking it.
:::

Your try job will show up on the [nss-try dashboard](https://treeherder.mozilla.org/#/jobs?repo=nss-try)
shortly after the push completes; `./mach try` also prints a direct Treeherder link for the
revision it pushed.

(try-syntax)=

## Try syntax

Try syntax selects which subset of Taskcluster builds and tests to run. The full form is:

```text
try: -b do -p all -u all -t all -e all
```

which is equivalent to not specifying any try syntax at all — it runs every available build
and test.

For the current set of valid platform, test, and tool tokens, run `./mach try` with no
arguments. The descriptions below cover what each flag means.

### Build types (`-b` / `--build`)

Two build types are available: `debug` and `opt`. Use `-b do` for both, `-b d` for
debug only, `-b o` for opt only. Default: `do`.

### Platforms (`-p` / `--platform`)

Specify any combination of the available platforms, like `-p linux,win64`, to choose where
your patch should be tested. Use `-p all` for every platform, or `-p none` for no platforms
(only useful when running tools-only jobs). Default: `all`.

### Unit tests (`-u` / `--unittests`)

Specify any combination of test suites, like `-u ec,gtest`. Use `-u all` for every suite,
`-u none` for no tests. Default: `none`.

### Tools (`-t` / `--tools`)

Specify any combination of tool jobs, like `-t clang-format,scan-build`. Use `-t all` for
every tool, `-t none` for none. Default: `none`.

### Extra builds (`-e` / `--extra-builds`)

Specify `all` or `none` to enable or disable extra builds. The number and type of extra
builds varies per platform. Default: `none`.

The cross-compiled Android (`android-aarch64`, `android-arm`, `android-x64`, `android-x86`)
and iOS (`ios-aarch64`, `ios-sim-aarch64`, `ios-sim-x64`) platforms only have extra builds,
so they need `-e all`. They are opt, static and build-only: no tests run on them.

### NSPR changes (`--nspr-patch`)

Mozilla does not maintain separate try infrastructure for NSPR, but you can test NSPR changes
on `nss-try`. Create a patch file with `hg diff` in the top-level nspr directory, name it
`nspr.patch`, and copy it into the main nss directory (`nss/nspr.patch`). Then add
`--nspr-patch` to your try syntax.

## Try syntax implementation

This section describes how the try syntax is implemented for `nss-try`.

### Decision task

After a new changeset is pushed to `nss-try` (or to the main NSS repository), Taskcluster
spawns a decision task that builds the task graph — i.e. decides what to build and which
tests to run. NSS uses [mozilla-taskgraph](https://hg.mozilla.org/mozilla-central/file/tip/taskcluster/)
driven by the in-tree configuration under `taskcluster/`: kind definitions in
`taskcluster/kinds/` (`build`, `test`, `tools`, `fuzz`, …) and target-task
selection in `taskcluster/nss_taskgraph/`.

### Task filter

The configuration above describes the complete task graph for all build types, platforms,
test suites, and tools. Before that graph is submitted back to Taskcluster, the decision task
filters tasks against the try syntax parsed from the commit message. The filter lives at:

`taskcluster/nss_taskgraph/target_tasks.py` — see `filter_try_syntax` and the
`nss_try_tasks` target task.

### Making changes

When changing the try-syntax filter — or any other part of the NSS Taskcluster CI — push to
`nss-try` before landing to confirm it behaves as expected. All Taskcluster CI changes can
be validated on `nss-try` before landing on the main repository.
