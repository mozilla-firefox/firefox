# Supported Build Hosts and Targets

```{eval-rst}
..  role:: strikethrough
```

There are three tiers of supported Firefox build hosts and targets.
These tiers represent the shared engineering priorities of the Mozilla project.

The "build host" is the machine that is performing the build of Firefox, and
the "build target" is the machine that will run the built Firefox application.
For example, if you were building Firefox for Android on your Linux computer, then the
Linux computer would be the "build host".

:::{note}
Sheriffs are in charge of monitoring the tree. Their definition for tiers
is for automation jobs, which tells a developer what is expected of them when
they land code. This document is about the tiers of supported build hosts and targets,
which tells a person compiling/using Firefox what they can expect from Mozilla.
See the [job tier definition](https://wiki.mozilla.org/Sheriffing/Job_Visibility_Policy#Overview_of_the_Job_Visibility_Tiers) for more information.
:::

(build-hosts)=

## Supported Build Hosts

While we want to help users resolve build-related issues on their systems, we
are unable to help resolve build system issues on all possible operating
systems and versions.

(tier-1-hosts)=

### Tier-1 Hosts and Toolchains

Support is available for the following **host operating systems** and versions
when building for a {ref}`Tier-1 Firefox build target<tier-1-targets>`, including
cross-compilation where available:

- Ubuntu Linux x86_64
  : - Current stable release
  - Previous stable release
  - Current LTS release
- Debian Linux x86_64
  : - Current stable release
  - Current testing release
- Fedora Linux x86_64
  : - Current stable release
  - Previous stable release
- macOS Intel and M1
  : - Current major macOS release
  - Previous major macOS release
- Windows x86_64
  : - Windows 10 with MozillaBuild Environment
  - Windows 11 with MozillaBuild Environment

### Tier-2 Hosts and Toolchains

In addition to the limitations outlined above in the Tier-1 list, our ability
to provide assistance with build issues using/targeting Tier-2
hosts/targets/compilers is not unbounded.

While we will endeavour to make a best effort to help resolve issues, you may
be referred to the relevant community maintainers for further support.

The Tier-2 hosts are:

- Other Linux x86_64 distributions and/or versions
- Older macOS versions
- Older Windows x86_64 versions

### Tier-3 Hosts and Toolchains

We cannot provide any guarantees of assistance in resolving build issues using
or targeting Tier-3 platforms.

## Supported Build Targets

(tier-1-targets)=

### Tier-1 Targets

The term **"Tier-1 platform"** refers to those platforms - CPU
architectures and operating systems - that are the primary focus of
Firefox development efforts. Tier-1 platforms are fully supported by
Mozilla's [continuous integration processes](https://treeherder.mozilla.org/) and the
{doc}`/tools/try/index`. Any proposed change to Firefox on these
platforms that results in build failures, test failures, performance
regressions or other major problems **will be reverted immediately**.

The **Tier-1 Firefox platforms** and their supported compilers are:

- Android on Linux x86-64, ARMv7 and ARMv8-A (clang)
- Linux x86-64 (gcc and clang), Linux/AArch64 (clang)
- macOS 10.15 and later on x86-64 and AArch64 (clang)
- Windows/x86, x86-64 and AArch64 (clang-cl)

Prior to Firefox 63, Windows/x86 and Windows/x86-64 relied on the MSVC
compiler; from **Firefox 63 onward MSVC is not supported**. Older 32-bit
x86 CPUs without SSE2 instructions such as the Pentium III and Athlon XP
are also **not considered Tier-1 platforms, and are not supported**.
Note also that while Windows/x86 and ARM/AArch64 are supported *as build
targets*, it is not possible to build Firefox *on* Windows/x86 or
Windows/AArch64 systems.

### Tier-2 Targets

**Tier-2 platforms** are actively maintained by the Mozilla community,
though with less rigorous requirements. Proposed changes resulting in
breakage or regressions limited to these platforms **may not immediately
result in reversion**. However, developers who break these platforms are
expected to work with platform maintainers to fix problems, and **may be
required to revert their changes** if a fix cannot be found.

The **Tier-2 Firefox platforms** and their supported compilers are:

- Windows/x86 (mingw-clang) - maintained by Tom Ritter and Jacek Caban
  \-

  - *Note that some features of this platform are disabled, as they
    require MS COM or the w32api project doesn't expose the necessary
    Windows APIs.*

### Tier-3 Targets

**Tier-3 platforms** have a maintainer or community which attempt to
keep the platform working. These platforms are **not supported by our
continuous integration processes**, and **Mozilla does not routinely
test on these platforms**, nor do we block further development on the
outcomes of those tests.

At any given time a Firefox built from source for these
platforms may or may not work correctly or build at all.

**Tier-3 Firefox platforms** include:

- Linux on various CPU architectures including ARM variants not listed
  above, PowerPC, and x86 CPUs - maintained by various Linux distributions
- FreeBSD/x86, x86-64, Aarch64 (clang) - [maintained by gecko@FreeBSD.org](https://www.freshports.org/www/firefox/)
- OpenBSD/x86, x86-64 (clang) - maintained by Landry Breuil
- NetBSD/x86-64 (gcc) - maintained by David Laight
- Solaris/x86-64, sparc64 (gcc) - maintained by Petr Sumbera
- {strikethrough}`Windows/x86-64 (mingw-gcc)` - Unsupported due to
  requirements for clang-bindgen

If you're filing a bug against Firefox on a Tier-3 platform (or any
combination of OS, CPU and compiler not listed above) please bear in
mind that Mozilla developers do not reliably have access to non-Tier-1
platforms or build environments. To be actionable bug reports against
non-Tier-1 platforms should include as much information as possible to
help the owner of the bug determine the cause of the problem and the
proper solution. If you can provide a patch, a regression range or
assist in verifying that the developer's patches work for your platform,
that would help a lot towards getting your bugs fixed and checked into
the tree.
