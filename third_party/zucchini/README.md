General Information
===================

This directory contains our vendoring for Zucchini, a binary diffing algorithm
from the chromium project. We use Zucchini to generate patches that summarize
how to transform a given version of Firefox binary files into their next
version. We ship these patches, compressed, within the MAR archives used for
partial updates. When an update is available, Firefox clients will download
such a MAR archive and feed it to `updater.exe`, which will apply the patches
to transform the binaries found on disk to their next version.

Zucchini patches are optimized to be small *after compression*, so that storing
and transferring them requires as few bytes as possible. Zucchini uses
specificities of the PE and ELF format to produce patches of small size for
Windows and Linux binaries. It also performs quite well on Apple Mach-O
binaries, even though it treats them as arbitrary data.

See the [upstream README.md file](
https://source.chromium.org/chromium/chromium/src/+/main:components/zucchini/README.md
) for technical definitions and low-level details.

Using Zucchini
==============

Zucchini support is available on Linux, macOS and Windows, behind the
`mozconfig` options `--enable-updater` and `--enable-zucchini`. It can be used
jointly with `--enable-bspatch` to preserve support for MBSPatch-formatted
patches. In this case, `updater.exe` will be able to distinguish Zucchini and
MBSPatch patches based on the contents of the patch file, since both kinds of
patches start with a different magic value: `"Zucc"` or `"MBDIFF10"`. See
`build/moz.configure/update-programs.configure` for more details.

No SEH In MinGW Builds
----------------------

Zucchini uses [SEH](
https://learn.microsoft.com/en-us/cpp/cpp/try-except-statement) to catch
`EXCEPTION_IN_PAGE_ERROR` exceptions, which Windows raises when an I/O error
occurs while accessing a memory-mapped file. MinGW compilers don't support SEH,
so `mozilla-config.h` replaces `__try` with `if (true)` and `__except` with
`else` when compiling Zucchini code, just like it does for our sandbox code.
Consequently, in MinGW builds, such an I/O error crashes `updater.exe` rather
than making Zucchini return an error status. The updater recovers from these
crashes thanks to its draft phase, see `toolkit/mozapps/update/updater.cpp`.

Build Artifacts
=============

`updater.exe`
-------------

Enabling Zucchini adds support for Zucchini-formatted patches in `updater.exe`.
See `toolkit/mozapps/update/updater.cpp` for the implementation.

`zucchini.exe`
--------------

Enabling Zucchini makes `./mach build` produce a standalone binary
`<obj-dir>/dist/bin/zucchini.exe`. Most importantly, this binary can generate a
Zucchini patch file `patch_file` that describes how to transform a file
`old_file` into another file `new_file`, as follows:

```
zucchini.exe -gen <old_file> <new_file> <patch_file> -keep
```

Run `zucchini.exe` without arguments to list all available features.

`zucchini-gtest.exe`
--------------------

Enabling Zucchini makes `./mach build` produce a standalone binary
`<obj-dir>/dist/bin/zucchini-gtest.exe`. This binary runs the upstream tests
from the chromium project to ensure that our vendoring works as expected.

Most Relevant Divergences With Upstream Code
============================================

No Support For DEX And ZTF
--------------------------

Upstream zucchini code has support binaries in DEX format (for Android) and ZTF
(Zucchini Text Format, for internal zucchini development). In constrast, we
only support PE (for Windows) and ELF (for Linux) and treat anything else as
raw data. This is defined in
`third_party/zucchini/chromium-shim/components/zucchini/buildflags.h`.

Reduced Logging Capability
--------------------------

We are less flexible than upstream code with respect to logging. We log to the
update log when running `updater.exe`, and to the standard output or error
stream when running `zucchini.exe` or `zucchini-gtest.exe`. Flags that control
chromium logging are unlikely to have any effect on our binaries. This is
implemented in `third_party/zucchini/chromium-shim/base/logging.cc`.

Different Split Of Code
-----------------------

Upstream build definitions split zucchini into two libraries `zucchini_lib` and
`zucchini_io`, and one executable `zucchini`. Our equivalents are a single
library named `zucchini` and an executable also named `zucchini`. The upstream
integration code (`zucchini_integration.cc`) is built in the executable rather
than the library, because the library is compiled with C++ exceptions enabled.

Different Integration Code
--------------------------

We chose to make zucchini fit with the logic that was already present in our
updater code by providing our own interface code in
`third_party/zucchini/moz_zucchini.{cc,h}`, rather than using the existing
upstream interfacing code (`zucchini_integration.cc`). This makes it easier to
have the updater support patches in both MBSPatch and Zucchini formats.

Testing
=======

Upstream Unit Tests
-------------------

You can run upstream unit tests locally by running `zucchini-gtest.exe` after
`./mach build`. If you have the skills required to integrate these tests into
our CI, [maybe you can help us with bug 1976057](
https://bugzilla.mozilla.org/show_bug.cgi?id=1976057)?

Updater Integration Tests
-------------------------

These are regular xpcshell tests than can be run locally and on the CI.
They are defined in `toolkit/mozapps/update/tests/unit_update_binary/` and
typically have `Zucchini` in the name of the file.

Directory Architecture And Conventions For Our Vendoring
========================================================

Upstream zucchini code comes directly from the chromium source tree. The
conventions described below are mostly inspired by those that we follow in
`security/sandbox`, which is also a vendoring based on the chromium source
tree.

`third_party/zucchini/chromium`
-------------------------------

This directory contains all the files that are derived from the chromium source
tree. Most of these files come directly from upstream, however we modify some
of them with patches. Our patches are present in
`third_party/zucchini/chromium-shim/patches` and they are already applied to
the code present in our source tree.

Any modification to an upstream file present in `third_party/zucchini/chromium`
must come with a matching patch, such that reapplying the vendoring process
yields the same source tree as the current one.

When we patch an upstream file, we apply the following convention:

- we wrap added code within `defined(MOZ_ZUCCHINI)` preprocessor guards;
- we wrap removed code within `!defined(MOZ_ZUCCHINI)` preprocessor guards.

This is simply a convention: we always compile Zucchini code with a definition
for `MOZ_ZUCCHINI`, so the behavior is the same as if the code within
`defined(MOZ_ZUCCHINI)` guards was simply added and the code within
`!defined(MOZ_ZUCCHINI)` guards was simply removed. This makes it easy to
identify and understand the modified portions of upstream code within our
vendoring.

`third_party/zucchini/chromium-shim`
------------------------------------

This directory follows the tree structure of `third_party/zucchini/chromium`,
but contains files that we completely write from scratch. We do this either
because the corresponding upstream file doesn't fit our needs, or because there
is no upstream file as it is a file that chromium's build system generates. The
files listed here can be thought of as living within
`third_party/zucchini/chromium`, we only keep them separated so that they are
easy to identify.

`third_party/zucchini/chromium/moz.yaml`
----------------------------------------

This file can be used with `./mach vendor` and lists the files that we import
from upstream before applying our patches to them. Compared to other
vendorings, we follow the following extra convention: we list any file present
in `third_party/zucchini/chromium-shim` within `moz.yaml`, but as a comment
(`#`) rather than a real item (`-`). This makes it easy to identify that these
files are already present in our tree but that we do not use the upstream
source code for them.
