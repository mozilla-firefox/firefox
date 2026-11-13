# Configuring Build Options

This document details how to configure Firefox builds.
Most of the time a `mozconfig` file is not required. The default
options are the most well-supported, so it is preferable to add as few
options as possible. Please read the following directions carefully
before building, and follow them in order. Skipping any step may cause
the build to fail, or the built software to be unusable. Build options,
including options not usable from the command-line, may appear in
"`confvars.sh`" files in the source tree.

(setting-up-a-mozconfig)=

## Using a `mozconfig` configuration file

The choice of which Mozilla project to build and other configuration
options can be configured in a `mozconfig` file. (It is possible to
manually call `configure` with command-line options, but this is not
recommended). The `mozconfig` file should be in your source directory
(that is, `/mozilla-central/mozconfig`).

Create a blank `mozconfig` file:

```bash
echo "# My first mozilla config" > mozconfig
```

If your mozconfig isn't in your source directory, you can also use the
`MOZCONFIG` environment variable to specify the path to your
`mozconfig`. The path you specify **must** be an **absolute** path or
else `client.mk` will not find it. This is useful if you choose to
have multiple `mozconfig` files for different projects or
configurations (see below for a full example). Note that in the
`export` example below the filename was not `mozconfig`. Regardless
of the name of the actual file you use, we refer to this file as the
`mozconfig` file in the examples below.

Setting the `mozconfig` path:

```bash
export MOZCONFIG=$HOME/mozilla/mozconfig-firefox
```

:::{note}
Calling the file `.mozconfig` (with a leading dot) is also
supported, but this is not recommended because it may make the file
harder to find. This will also help when troubleshooting because
people will want to know which build options you have selected and
will assume that you have put them in your `mozconfig` file.
:::

## `mozconfig` contains two types of options

- Options prefixed with `mk_add_options` are passed to
  `client.mk`. The most important of these is `MOZ_OBJDIR`, which
  controls where your project gets built (also known as the object
  directory).
- Options prefixed with `ac_add_options` are passed to `configure`,
  and affect the build process.

## Building with an objdir

This means that the source code and object files are not intermingled in
your directory system and you can build multiple projects (e.g.,
Firefox and Thunderbird) from the same source tree. If you do not
specify a `MOZ_OBJDIR`, it will be automatically set to
`@TOPSRCDIR@/obj-@CONFIG_GUESS@`.

If you need to re-run `configure`, the easiest way to do it is using
`./mach configure`; running `configure` manually is strongly
discouraged.

Adding the following line to your `mozconfig` allows you to change the
objdir:

```bash
mk_add_options MOZ_OBJDIR=@TOPSRCDIR@/obj-@CONFIG_GUESS@
```

It is a good idea to have your objdir name start with `obj` so that
the VCS ignores it.

Sometimes it can be useful to build multiple versions of the source
(such as with and without diagnostic asserts). To avoid the time it
takes to do a full rebuild, you can create multiple `mozconfig` files
which specify different objdirs. For example, a `mozconfig-dbg`:

```bash
mk_add_options MOZ_OBJDIR=@TOPSRCDIR@/obj-ff-dbg
ac_add_options --enable-debug
```

and a `mozconfig-rel-opt`:

```bash
mk_add_options MOZ_OBJDIR=@TOPSRCDIR@/obj-ff-rel-opt
ac_add_options --disable-debug
ac_add_options --enable-optimize
```

allow for building both versions by specifying the configuration via
the `MOZCONFIG` environment variable:

```bash
env MOZCONFIG=/path/to/mozconfig-dbg ./mach build
env MOZCONFIG=/path/to/mozconfig-rel-opt ./mach build
```

Don't forget to set the `MOZCONFIG` environment variable for the
`mach run` command as well.

Be aware that changing your `mozconfig` will require the configure
process to be rerun and therefore the build will take considerably
longer, so if you find yourself changing the same options regularly, it
may be worth having a separate `mozconfig` for each. The main downside
of this is that each objdir will take up a significant amount of space
on disk.

## Parallel compilation

:::{note}
The build system automatically makes an intelligent guess
for how many CPU cores to use when building. The option below is
typically not needed.
:::

Most modern systems have multiple cores or CPUs, and they can be
optionally used concurrently to make the build faster. The `-j` flag
controls how many parallel builds will run concurrently. You will see
(diminishing) returns up to a value approximately 1.5× to 2.0× the
number of cores on your system.

```bash
mk_add_options MOZ_PARALLEL_BUILD=4
```

If your machine is overheating, you might want to try a lower value.

## Choose a project

The `--enable-project=project` flag is used to select a project to
build. Firefox is the default.

Choose one of the following options to add to your `mozconfig` file:

**Browser (Firefox)** (the default):

```
ac_add_options --enable-project=browser
```

**Mail (Thunderbird)**:

```
ac_add_options --enable-project=comm/mail
```

**Mozilla Suite (SeaMonkey)**:

```
ac_add_options --enable-project=suite
```

**Calendar (Lightning Extension, uses Thunderbird)**:

```
ac_add_options --enable-project=comm/mail
ac_add_options --enable-calendar
```

## Selecting build options

The build options you choose depends on what project you are
building and what you will be using the build for. If you want to use
the build regularly, you will want a release build without extra
debugging information; if you are a developer who wants to hack the
source code, you probably want a non-optimized build with extra
debugging macros.

There are many options recognized by the configure script which are
special-purpose options intended for embedders or other special
situations, and should not be used to build the full suite/XUL
projects. The full list of options can be obtained by running
`./mach configure -- --help`.

:::{warning}
Do not use a configure option unless you know what it does.
The default values are usually the right ones. Each additional option
you add to your `mozconfig` file reduces the chance that your build
will compile and run correctly.
:::

The following build options are very common:

### sccache

[SCCache](https://github.com/mozilla/sccache) allows speeding up subsequent
C / C++ builds by caching compilation results. Unlike
[ccache](https://ccache.dev), it also allows caching Rust artifacts, and
supports [distributed compilation](https://github.com/mozilla/sccache/blob/master/docs/DistributedQuickstart.md).

In order to enable `sccache` for Firefox builds, you can use
`ac_add_options --with-ccache=sccache`.

From version 0.7.4, sccache local builds are using the `preprocessor cache mode`
by default. With a hot cache, it decreases the build time by a factor of 2 to 3
compared the previous method. This feature works like the [direct mode in ccache](https://ccache.dev/manual/3.7.9.html#_the_direct_mode),
using a similar way to handle caching and dependencies.

:::{note}
When using sccache, because of the operation on the files and storage,
the initial build of Firefox will be slower.
:::

In automation, the build system sets `SCCACHE_BASEDIRS` to the object and
source directories, as well as the sysroot and SDKs. sccache rewrites paths
under those prefixes to be relative before computing cache keys, so builds in
different checkouts share cache entries instead of missing on differing
absolute paths.

Local builds don't set it by default. With the local disk cache's preprocessor
cache mode, sccache can return an object file built in another checkout whose
headers differ ([sccache issue 2863](https://github.com/mozilla/sccache/issues/2863)).
If you set `SCCACHE_BASEDIRS` yourself, also set `SCCACHE_DIRECT=false` and
delete the `preprocessor` directory in your sccache cache. sccache reads both
variables when its server starts, so stop it (`sccache --stop-server`) after
changing them.

### buildcache

[BuildCache](https://gitlab.com/bits-n-bites/buildcache) is another compiler
caching tool that can speed up subsequent C / C++ and Rust builds by caching
compilation results. Unlike `ccache`, it also supports caching Rust artifacts.

In order to enable `buildcache` for Firefox builds, you can use
`ac_add_options --with-ccache=buildcache`.

### Optimization

`ac_add_options --enable-optimize`

: Enables the default compiler optimization options

  :::{note}
  This is enabled by default
  :::

`ac_add_options --enable-optimize=-O2`

: Chooses particular compiler optimization options. In most cases, this
  will not give the desired results, unless you know the Mozilla
  codebase very well; note, however, that if you are building with the
  Microsoft compilers, you probably **do** want this as `-O1` will
  optimize for size, unlike GCC.

`ac_add_options --enable-debug`

: Enables assertions in C++ and JavaScript, plus other debug-only code.
  This can significantly slow a build, but it is invaluable when
  writing patches. **People developing patches (especially in C++)
  should generally use this option.**

`ac_add_options --disable-optimize`

: Disables compiler optimization. This makes it much easier to step
  through code in a debugger.

`ac_add_options --enable-release`

: Enables more conservative, release engineering-oriented options. This may
  slow down builds. This also turns on full optimizations for Rust. Note this
  is the default when building release/beta/esr.

`ac_add_options --enable-debug-js-modules`

: Enable only JavaScript assertions. This is useful when working
  locally on JavaScript-powered components like the DevTools. This will
  help catch any errors introduced into the JS code, with less of a
  performance impact compared to the `--enable-debug` option.

`export RUSTC_OPT_LEVEL=2`

: Enable full optimizations for Rust code.

You can make an optimized build with debugging symbols. See [Building
with Debug Symbols](building_with_debug_symbols.md).

### Building as Beta or Release

`ac_add_options --as-milestone=release`

: This makes it easy to build nightly with a release or beta configuration to
  test the different ifdef behaviors. To do a full beta simulation see
  [Sheriffing/How To/Beta simulations](https://wiki.mozilla.org/Sheriffing/How_To/Beta_simulations).

- `beta`
- `release`

`early-beta` and `late-beta` are deprecated aliases of `beta`, accepted only
transitionally so that existing mozconfigs keep working. The distinct early and
late beta periods are being phased out, so both do exactly what `beta` does.
Update your mozconfig to `--as-milestone=beta`; the aliases will be removed.

### Tests

`ac_add_options --disable-tests`

: By default, many auxiliary test programs are built, which can
  help debug and patch the mozilla source. Disabling these tests can
  speed build time and reduce disk space considerably. Developers
  should generally not use this option.

### Localization

`mk_add_options MOZ_CO_LOCALES=ISOcode`

: TBD.

`ac_add_options --enable-ui-locale=ISOcode`

: TBD.

`ac_add_options --with-l10n-base=/path/to/base/dir`

: TBD.

### Other Options

`mk_add_options AUTOCLOBBER=1`

: If a clobber would be required before a build, this will cause mach
  to clobber and continue with the build instead of asking the user to
  manually clobber and exiting.

`ac_add_options --enable-warnings-as-errors`

: This makes compiler warnings into errors which fail the build. This
  can be useful since certain warnings coincide with reviewbot lints
  which must be fixed before merging.

(example-mozconfig-files)=

## Example `mozconfig` Files

Mozilla's official builds use mozconfig files from the appropriate
directory within each repository.

:::{warning}
These `mozconfig` files are taken from production builds
and are provided as examples only. It is recommended to use the default
build options, and only change the properties from the list above as
needed. The production builds aren't really appropriate for local
builds."
:::

- ```{rubric} Firefox, [Debugging Build (macOS 64bits)](https://github.com/mozilla-firefox/firefox/blob/main/browser/config/mozconfigs/macosx64/debug)
  :name: Firefox.2C_Default_Release_Configuration
  ```

## Building multiple projects from the same source tree

It is possible to build multiple projects from the same source tree,
as long as you [use a different objdir](#building-with-an-objdir) for
each project.

You need to create multiple `mozconfig` files.

As an example, the following steps can be used to build Firefox and
Thunderbird. You should first create three `mozconfig` files.

`mozconfig-common`:

```
# add common options here, such as making an optimized release build
mk_add_options MOZ_PARALLEL_BUILD=4
ac_add_options --enable-optimize --disable-debug
```

`mozconfig-firefox`:

```
# include the common mozconfig
. ./mozconfig-common

# Build Firefox
mk_add_options MOZ_OBJDIR=@TOPSRCDIR@/obj-firefox
ac_add_options --enable-project=browser
```

`mozconfig-thunderbird`:

```
# include the common mozconfig
. ./mozconfig-common

# Build Thunderbird
mk_add_options MOZ_OBJDIR=@TOPSRCDIR@/obj-thunderbird
ac_add_options --enable-project=comm/mail
```

To build Firefox, run the following commands:

```
export MOZCONFIG=/path/to/mozilla/mozconfig-firefox
./mach build
```

To build Thunderbird, run the following commands:

```
export MOZCONFIG=/path/to/mozilla/mozconfig-thunderbird
./mach build
```

### Using mozconfigwrapper

Mozconfigwrapper is similar to using multiple mozconfig files except
that it abstracts and hides them so you don't have to worry about where
they live or which ones you've created. It also saves you from having to
export the MOZCONFIG variable each time. For information on installing
and configuring mozconfigwrapper, see
<https://github.com/ahal/mozconfigwrapper>.

### Changing build options per directory -- the moz.build hook

Adding a statement like the following to a `mozconfig` file:

```
ac_add_options MOZ_BUILD_HOOK=/absolute/path/to/file/buildhook.py
```

will cause this file to be appended to each `moz.build` file of the tree. It
is recommended to place this file outside of the tree, so that it is not wiped
when cleaning up the tree via `git clean`.

This hook file is written in the same subset of Python as `moz.build` files.

Possibilities are endless, but this is particularly useful to tweak compiler
options for selected directories, such as disabling optimizations to have a
better debugging experience without having the entire browser being very slow:

```python
nonopt = (
    "dom/media/",
    "media/ffvpx/",
    "media/libcubeb/",
)
if RELATIVEDIR.startswith(nonopt):
    COMPILE_FLAGS["OPTIMIZE"] = []
```

will make all compilation units at or under those three paths have no
optimization.

Another useful thing to set per directory is `FILES_PER_UNIFIED_FILE=1` to
disable {ref}`unified builds<unified-builds>`.
