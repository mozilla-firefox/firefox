(localized-builds)=

# Localized Builds

## Localization repacks

To save on build time, the build system and automation collaborate to allow
downloading a packaged en-US Firefox, performing some locale-specific
post-processing, and re-packaging a locale-specific Firefox. Such artifacts
are termed "single-locale language repacks". There is another concept of a
"multi-locale language build", which is more like a regular build and less
like a re-packaging post-processing step.

### Instructions for single-locale repacks for developers

This assumes that `$AB_CD` is the locale you want to repack with; you
find the available localizations from [firefox-l10n](https://github.com/mozilla-l10n/firefox-l10n).

1. You must have a built and packaged object directory, or a pre-built
   `en-US` package.

   ```shell
   ./mach build
   ./mach package
   ```

2. Repackage using the locale-specific changes.

   ```shell
   ./mach build installers-$AB_CD
   ```

You should find a re-packaged build at `OBJDIR/dist/`, and a runnable binary
in `OBJDIR/dist/l10n-stage/$AB_CD/`. The `installers` target runs quite a few
things for you, including getting the localizations from
<https://github.com/mozilla-l10n/firefox-l10n> (or updating it if already
present). It will clone them into `~/.mozbuild/l10n-central`. If you prefer to
have the l10n repositories at a different location on your disk, you can point
to the directory via

```shell
ac_add_options --with-l10n-base=/make/this/a/absolute/path
```

This build also packages a language pack.

### Instructions for language packs

Language packs are extensions that contain just the localized resources. Building
them doesn't require an actual build, but they're only compatible with the
`mozilla-central` source they're built with.

```shell
./mach build langpack-$AB_CD
```

This target shares much of the logic of the `installers-$AB_CD` target above,
and does the check-out of the localization repository etc. It doesn't require
a package or a build, though. The generated language pack is in
`OBJDIR/dist/$(MOZ_PKG_PLATFORM)/xpi/`.

:::{note}
Despite the platform-dependent location in the build directory, language packs
are platform independent, and the content that goes into them needs to be
built in a platform-independent way.
:::

### Instructions for multi-locale builds

If you want to create a single build with multiple locales, you will do

1. Create a build and package

   ```shell
   ./mach build
   ./mach package
   ```

2. Create the multi-locale package:

   ```shell
   ./mach package-multi-locale --locales de it zh-TW
   ```

On Android, this produces a multi-locale GeckoView AAR and multi-locale APKs,
including GeckoViewExample. You can test different locales by changing your
Android OS locale and restarting GeckoViewExample. You'll need to install with
the `MOZ_CHROME_MULTILOCALE` variable set, like:

```shell
env MOZ_CHROME_MULTILOCALE=en-US,de,it,zh-TW ./mach android install-geckoview_example
```

### General flow of repacks

The general flow of the locale repacks is controlled by
`$MOZ_BUILD_APP/locales/Makefile.in` and `toolkit/locales/l10n.mk`, plus
the packaging build system. The three main entry points above all trigger
related build flows:

1. Get the localization repository, if needed
2. Run `l10n-merge`
3. Copy l10n files to `dist`, with minor differences here between `l10n-%` and `chrome-%`
4. Repackage and package

Details on l10n-merge are described in its own section below.
The copying of files is driven by the `l10n-manifest.json` that the build
backend emits from `jar.mn` and `LOCALIZED_FILES`, in the few source
directories that include localizable files. `l10n-%` is used for repacks,
`chrome-%` for multi-locale packages. The repackaging is done by
`python/mozbuild/mozbuild/action/l10n_repackage.py`, starting from an existing
package. It strips existing `chrome` l10n resources, and adds localizations
and metadata.

Language packs don't require repackaging. The windows installers are generated
by merely packaging an existing repackaged zip into to an installer.

## Exposing strings

The localization flow handles a few file formats in well-known locations in the
source tree.

Alongside being built by including the directory in `$MOZ_BUILD_APP/locales/Makefile.in`
and respective entries in a `jar.mn`, we also have configuration files tailored
to localization tools and infrastructure. They're also controlling which
files l10n-merge handles, and how.

These configurations are TOML files. They're part of the bigger
localization ecosystem at Mozilla, and [the documentation about the
file format](http://moz-l10n-config.readthedocs.io/en/latest/fileformat.html)
explains how to set them up, and what the entries mean. In short, you find

```text
[paths]
reference = browser/locales/en-US/**
l10n = {l}browser/**
```

to add a directory for all localizations. Changes to these files are best
submitted for review by :Pike or :flod.

These configuration files are the future, and right now, we still have
support for the previous way to configuring l10n, which is described below.

The locations are commonly in directories like

{file}`browser/``locales/en-US/`{file}`subdir/file.ext`

The first thing to note is that only files beneath {file}`locales/en-US` are
exposed to localizers. The second thing to note is that only a few directories
are exposed. Which directories are exposed is defined in files called
`l10n.ini`, which are at a
[few places](https://searchfox.org/mozilla-central/search?q=path%3Al10n.ini&redirect=true)
in the source code.

An example looks like this

```ini
[general]
depth = ../..

[compare]
dirs = browser
    browser/branding/official

[includes]
toolkit = toolkit/locales/l10n.ini
```

This tells the l10n infrastructure three things:

- resolve the paths against the directory two levels up
- include files in {file}`browser/locales/en-US` and
  {file}`browser/branding/official/locales/en-US`
- load more data from {file}`toolkit/locales/l10n.ini`

For projects like Thunderbird and SeaMonkey in `comm-central`, additional
data needs to be provided when including an `l10n.ini` from a different
repository:

```ini
[include_toolkit]
type = hg
mozilla = mozilla-central
repo = https://hg.mozilla.org/
l10n.ini = toolkit/locales/l10n.ini
```

This tells the l10n infrastructure where to find the repository, and where inside
that repository the `l10n.ini` file is. This is needed because for local
builds, {file}`mail/locales/l10n.ini` references
{file}`mozilla/toolkit/locales/l10n.ini`, which is where the comm-central
build setup expects toolkit to be.

Now that the directories exposed to l10n are known, we can talk about the
supported file formats.

### File formats

The following file formats are known to the l10n tool chains:

Fluent

: Used in Firefox UI, both declarative and programmatically.

Properties

: Used from JavaScript and C++. When used from js, also comes with
  plural support (avoid if possible).

ini

: Used by the crashreporter and updater, avoid if possible.

Adding new formats involves changing various different tools, and is strongly
discouraged.

### Exceptions

Generally, anything that exists in `en-US` needs a one-to-one mapping in
all localizations. There are a few cases where that's not wanted, notably
around locale configuration and locale-dependent metadata.

For optional strings and files, l10n-merge won't add `en-US` content if
the localization doesn't have that content.

For the TOML files, the
[\[[filters]\] documentation](https://moz-l10n-config.readthedocs.io/en/latest/fileformat.html#filters)
is a good reference. In short, filters match the localized source code, optionally
a `key`, and an action. An example like

```toml
[filters]
path = "{l}calendar/chrome/calendar/calendar-event-dialog.properties"
key = "re:.*Nounclass[1-9].*"
action = "ignore"
```

indicates that the matching messages in `calendar-event-dialog.properties` are optional.

For the legacy ini configuration files, there's a Python module
`filter.py` next to the main `l10n.ini`, implementing {py:func}`test`, with the following
signature

```python
def test(mod, path, entity=None):
    if does_not_matter:
        return "ignore"
    if show_but_do_not_merge:
        return "report"
    # default behavior, localizer or build need to do something
    return "error"
```

For any missing file, this function is called with `mod` being
the *module*, and `path` being the relative path inside
{file}`locales/en-US`. The module is the top-level dir as referenced in
{file}`l10n.ini`.

For missing strings, the {py:data}`entity` parameter is the key of the string
in the en-US file.

## l10n-merge

The chrome registry in Gecko doesn't support fallback from a localization to `en-US` at runtime.
Thus, the build needs to ensure that the localization as it's built into
the package has all required strings, and that the strings don't contain
errors. To ensure that, we're *merging* the localization and `en-US`
at build time, nick-named l10n-merge.

For Fluent, we're also removing erroneous messages. For many errors in Fluent,
that's cosmetic, but when a localization has different values or attributes
on a message, that's actually important so that the DOM bindings of Fluent
can apply the translation without having to load the `en-US` source to
compare against.

The process can be manually triggered via

```bash
$> ./mach build merge-$AB_CD
```

It creates another directory in the object dir, {file}`browser/locales/merge-dir/$AB_CD`, in
which the sanitized files are stored. The actual repackaging process only looks
in the merged directory, so the preparation steps of l10n-merge need to ensure
that all files are generated or copied.

l10n-merge modifies a file if it supports the particular file type, and there
are missing strings which are not filtered out, or if an existing string
shows an error. See the Checks section below for details. If the files are
not modified, l10n-merge copies them over to the respective location in the
merge dir.

### Checks

As part of the build and other localization tool chains, we run a variety
of source-based checks. Think of them as linters.

The suite of checks is usually determined by file type, i.e., there's a
suite of checks for Fluent files and one for properties files, etc.

### Localizations

Now that we talked in-depth about how to expose content to localizers,
where are the localizations?

We host all locales in a git monorepo. All of our
localizations can be found on <https://github.com/mozilla-l10n/firefox-l10n>.

You can search inside our localized files on
[Transvision](https://transvision.mozfr.org/).
