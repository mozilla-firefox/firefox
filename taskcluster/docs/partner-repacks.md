# Partner repacks

(partner-repacks-1)=

We create slightly-modified Firefox releases for some extra audiences

- EME-free builds, which disable DRM plugins by default
- Funnelcake builds, which are used for Mozilla experiments
- partner builds, which customize Firefox for external partners

We use the phrase "partner repacks" to refer to all these builds because they
use the same process of repacking regular Firefox releases with additional files.
The specific differences depend on the type of build.

We produce partner repacks for some beta builds, and for release builds, as part of the release
automation. We don't produce any files to update these builds as they are handled automatically
(see [updates]).

We also produce {doc}`partner-attribution` builds, which are Firefox Windows installers with a cohort identifier
added.

## Parameters & Scheduling

Partner repacks have a number of parameters which control how they work:

- `release_enable_emefree`
- `release_enable_partner_repack`
- `release_partner_config`
- `release_partner_build_number`
- `release_partners`

We split the repacks into two 'paths', EME-free and everything else, to retain some
flexibility over enabling/disabling them separately. This costs us some duplication of the kinds
in the repacking stack. The two enable parameters are booleans to turn these two paths
on/off. We set them in shipit's [is_partner_enabled()](https://github.com/mozilla-releng/shipit/blob/main/api/src/shipit_api/admin/release.py#L93) when starting a
release. They're both true for Firefox betas >= b8 and releases, but otherwise disabled.

`release_partner_config` is a dictionary of configuration data which drives the task generation
logic. It's usually looked up during the release promotion action task, using the Github
GraphQL API in the `gecko_taskgraph.util.partners.get_partner_config_by_url()` function, with the
url defined in [taskcluster/config.yml](https://searchfox.org/mozilla-release/search?q=regexp%3A^partner+path%3Aconfig.yml&redirect=true).

That lookup happens only once per release, in the first promotion action that needs it —
normally `promote`. Later phases inherit the resolved config from the most recent previous
graph's `parameters.yml`, so that every phase repacks and publishes the same set of partners.
A partner added to a `default.xml` manifest partway through a release is therefore not picked
up by that release's `push` and `ship` phases; without this, they would create repack tasks
depending on cached tasks whose artifacts predate the new partner (bug 2071912). Use the next
release, or an off-cycle `promote_firefox_partner_repack` run — those flavors deliberately look
the config up again — to build a newly added partner.

`release_partner_build_number` is an integer used to create unique upload paths in the firefox
candidates directory, while `release_partners` is a list of partners that should be
repacked (i.e. a subset of the whole config). Both are intended for use when respinning a few partners after
the regular Firefox has shipped. More information on that can be found in the
[RelEng Docs](https://moz-releng-docs.readthedocs.io/en/latest/procedures/misc-operations/off-cycle-partner-repacks-and-funnelcake.html).

Most of the machine time for generating partner repacks takes place in the `promote` phase of the
automation, or `promote_rc` in the case of X.0 release candidates. The EME-free builds are copied into the
Firefox releases directory in the `push` phase, along with the regular bits.

## Configuration

We need some configuration to know *what* to repack, and *how* to do that. The *what* is defined by
default.xml manifests, as used with the [repo](https://gerrit.googlesource.com/git-repo) tool
for git. The [default.xml for EME-free](https://github.com/mozilla-partners/mozilla-EME-free-manifest/blob/master/default.xml) illustrates this:

```
<?xml version="1.0" ?>
<manifest>
  <remote fetch="git@github.com:mozilla-partners/" name="mozilla-partners"/>
  <remote fetch="git@github.com:mozilla/" name="mozilla"/>

  <project name="repack-scripts" path="scripts" remote="mozilla-partners" revision="master"/>
  <project name="build-tools" path="scripts/tools" remote="mozilla" revision="master"/>
  <project name="mozilla-EME-free" path="partners/mozilla-EME-free" remote="mozilla-partners" revision="master"/>
</manifest>
```

The repack-scripts and build-tools repos are found in all manifests, and then there is a list of
partner repositories which contain the *how* configuration. Some of these repos are not publicly
visible.

A partner repository may contain multiple configurations inside the `desktop` directory. Each
subdirectory must contain a `repack.cfg` and a `distribution` directory, the latter
containing the customizations needed. Here's [EME-free's repack.cfg](https://github.com/mozilla-partners/mozilla-EME-free/blob/master/desktop/mozilla-EME-free/repack.cfg):

```
aus="mozilla-EMEfree"
dist_id="mozilla-EMEfree"
dist_version="1.0"
linux-i686=false
linux-x86_64=false
locales="ach af an ar"  # truncated for display here
mac=true
win32=true
win64=true
output_dir="%(platform)s-EME-free/%(locale)s"
```

Note the list of locales and boolean toggles for enabling platforms.

All customizations will be placed in the `distribution` directory at the root of the Firefox
install directory, or in the case of OS X in `Firefox.app/Contents/Resources/distribution/`. A
`distribution.ini` file is the minimal requirement, here's an example from [EME-free](https://github.com/mozilla-partners/mozilla-EME-free/blob/master/desktop/mozilla-EME-free/distribution/distribution.ini):

```
# Partner Distribution Configuration File
# Author: Mozilla
# Date: 2015-03-27

[Global]
id=mozilla-EMEfree
version=1.0
about=Mozilla Firefox EME-free

[Preferences]
media.eme.enabled=false
app.partner.mozilla-EMEfree="mozilla-EMEfree"
```

Extensions and other customizations might also be included in repacks.

## Repacking process

The stack of tasks to create partner repacks is broadly similar to localised nightlies and
regular releases. The basic form is

- partner repack - insert the customisations into the regular builds
- signing - sign the internals which will become the installer (Mac only)
- repackage - create the "installer" (Mac and Windows)
- chunking dummy - a linux only bridge to ...
- repackage signing - sign the "installers" (mainly Windows)
- beetmover - move the files to a partner-specific destination
- beetmover checksums - possibly beetmove the checksums from previous step

Some key divergences are:

- all intermediate artifacts are uploaded with a `releng/partner` prefix
- we don't insert any binaries on Windows so no need for internal signing
- there's no need to create any complete mar files at the repackage step
- we only need beetmover checksums for EME-free builds

### Partner repack

- kinds: `release-partner-repack` `release-eme-free-repack`
- platforms: Typically all (but depends on what's enabled by partner configuration)
- upstreams: `build-signing` `l10n-signing`

There is one task per platform in this step, running [taskcluster/scripts/release/partner-repack.sh](https://searchfox.org/firefox-main/source/taskcluster/scripts/release/partner-repack.sh) to fetch the repack manifests and then perform the repacks.
The actual repacking is done by [python/mozrelease/mozrelease/partner_repack.py](https://searchfox.org/firefox-main/source/python/mozrelease/mozrelease/partner_repack.py).

It takes as input the build-signing and l10n-signing artifacts, which are all zip/tar.gz/tar.xz
archives, simplifying the repack process by avoiding dmg and exe. Windows produces `target.zip`
& `setup.exe`, Mac is `target.tar.gz`, Linux is the final product `target.tar.xz`
(beetmover handles pretty naming as usual).

### Signing

- kinds: `release-partner-repack-mac-signing` `release-partner-repack-mac-notarization`
- platforms: Mac
- upstreams: `release-partner-repack` `release-eme-free-repack`

We chunk the single partner repack task out to a signing task with 5 artifacts each. For
example, EME-free will become 19 tasks. We collect the target.tar.gz from the
upstream, and return a signed target.tar.gz. We use a `target.dmg` artifact for
nightlies/regular releases, but this is converted to `target.tar.gz` by the signing
scriptworker before sending it to the signing server, so partners are equivalent. The `mac-signing` task
signs the binary, and then `mac-notarization` submits it to Apple and staples the ticket to it.

### Repackage

- kinds: `release-partner-repack-repackage` `release-eme-free-repack-repackage`

- platforms: Mac & Windows

- upstreams:

  > - Mac: `release-partner-signing` `release-eme-free-signing`
  > - Windows: `release-partner-repack` `release-eme-free-repack`

Mac has a repackage job for each of the signing tasks. Windows repackages are chunked here to
the same granularity as mac. Takes `target.zip` & `setup.exe` to produce `target.exe` on
Windows, and `target.tar.gz` to produce `target.dmg` on Mac. There's no need to produce any
complete.mar files here like regular release bits do because we can reuse those.

### Chunking dummy

- kinds: `release-partner-repack-chunking-dummy`
- platforms: Linux
- upstreams: `release-partner-repack`

We're need Linux chunked at the next step so this dummy takes care of that for the relatively simple path
Linux follows. One task per sub config+locale combination, the same as Windows and Mac. This doesn't need to
exist for EME-free because we don't need to create Linux builds there.

### Repackage Signing

- kinds: `release-partner-repack-repackage-signing` `release-eme-free-repack-repackage-signing`

- platforms: All

- upstreams:

  > - Mac & Windows: `release-partner-repackage` `release-eme-free-repackage`
  > - Linux: `release-partner-repack-chunking-dummy`

This step GPG signs all platforms, and authenticode signs the Windows installer.

### Beetmover

- kinds: `release-partner-repack-beetmover` `release-eme-free-repack-beetmover`
- platforms: All
- upstreams: `release-partner-repack-repackage-signing` `release-eme-free-repack-repackage-signing`

Moves and renames the artifacts to their public location in the [candidates directory](https://archive.mozilla.org/pub/firefox/candidates/). Each task will
have the `project:releng:beetmover:action:push-to-partner` and
`project:releng:beetmover:bucket:release` scopes. There's a separate partner
code path in [beetmoverscript](https://github.com/mozilla-releng/scriptworker-scripts/tree/master/beetmoverscript).

### Beetmover checksums

- kinds: `release-eme-free-repack-beetmover-checksums`
- platforms: Mac & Windows
- upstreams: `release-eme-free-repack-repackage-beetmover`

The EME-free builds should be present in our SHA256SUMS file and friends ([e.g.](https://archive.mozilla.org/pub/firefox/releases/61.0/SHA256SUMS)) so we beetmove the target.checksums from
the beetmover tasks into the candidates directory. They get picked up by the
`release-generate-checksums` kind.

(updates)=

## Updates

It's very rare to need to update a partner repack differently from the original
release build but we retain that capability. A partner build with distribution name `foo`,
based on a release Firefox build, will query for an update on the `release-cck-foo` channel. If
the update server [Balrog](http://mozilla-balrog.readthedocs.io/en/latest/) finds no rule for
that channel it will fallback to the `release` channel. The update files for the regular releases do not
modify the `distribution/` directory, so the customizations are not modified.

[Bug 1430254](https://bugzilla.mozilla.org/show_bug.cgi?id=1430254) is an example of an exception to this
logic.
