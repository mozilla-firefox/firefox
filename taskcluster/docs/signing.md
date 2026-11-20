# Signing

## Overview

Our [code signing] happens in discrete tasks, for both performance reasons
and to limit which machines have access to the signing servers and keys.

In general, the binary-to-be-signed is generated in one task, and the request
to sign it is in a second task. We verify the request via the [chain of trust],
sign the binary, then upload the signed binary or original binary + detached
signature as artifacts.

## How the Task Works

[Scriptworker] verifies the task definition and the upstream tasks until it
determines the graph comes from a trusted tree; this is [chain of trust]
verification. Part of this verification is downloading and verifying the shas
of the `upstreamArtifacts` in the task payload.

An example signing task payload:

```
{
  "payload": {
    "upstreamArtifacts": [{
      "paths": ["public/build/target.dmg"],
      "formats": ["macapp"],
      "taskId": "abcde",
      "taskType": "build"
    }, {
      "paths": ["public/build/target.tar.gz"],
      "formats": ["gcp_prod_autograph_gpg"],
      "taskId": "12345",
      "taskType": "build"
    }]
  }
}
```

In the above example, scriptworker would download the `target.dmg` from task
`abcde` and `target.tar.gz` from task `12345` and verify their shas and
task definitions via [chain of trust] verification. Then it will launch
[signingscript], which requests a signing token from the signing server pool.

Signingscript determines it wants to sign `target.dmg` with the `macapp`
format, and `target.tar.gz` with the `gcp_prod_autograph_gpg` format. Each
of the [signing formats] has their own behavior. After performing any
format-specific checks or optimizations, it calls [signtool] to submit the
file to the signing servers and poll them for signed output. Once it downloads
all of the signed output files, it exits and scriptworker uploads the signed
binaries.

We can specify multiple paths from a single task for a given set of formats,
and multiple formats for a given set of paths.

## Signing kinds

We currently have multiple signing kinds. These fall into several categories:

**Build internal signing**: Certain package types require the internals to be signed.
For certain package types, e.g. exe or dmg, we extract the internal binaries
(e.g. xul.dll) and sign them. This is true for certain zipfiles, exes, and dmgs;
we need to sign the internals before we [re]create the package. For linux
tarballs, we don't need special packaging, so we can sign everything in this
task. These kinds include `build-signing` and `l10n-signing`.

**Build repackage signing**: Once we take the signed internals and package them
(known as a `repackage`), certain formats require a signed external package.
If we have created an update MAR file from the signed internals, the MAR
file will also need to be signed. These kinds include `repackage-signing`,
`release-eme-free-repack-repackage-signing`, and `release-partner-repack-repackage-signing`.

`release-source-signing` and `partials-signing` sign the release source tarball
and partial update MARs.

**Mac signing and notarization**: For mac, we have `*-mac-signing`, which signs the app and pkg, `*-mac-notarization` submits to Apple and staples the resulting ticket to the binaries.

We generate signed checksums at the top of the releases directories, like
in [60.0]. To generate these, we have the checksums signing kinds, including
`release-generate-checksums-signing`, `checksums-signing`, and
`release-source-checksums-signing`

(signing-formats)=

## Signing formats

The known signingscript formats are listed in the fourth column of the
[signing password files].

The formats are specified in the `upstreamArtifacts` list-of-dicts.
`gcp_prod_autograph_gpg` signing results in a detached `.asc` signature
file. Because of its nature, we gpg-sign at the end if given multiple formats
for a given set of files.

`jar` signing is Android apk signing. After signing, we `zipalign` the apk.
This includes the `focus-jar` format, which is just a way to specify a different
set of keys for the Focus app.

`macapp` signing accepts either a `dmg` or `tar.gz`; it converts `dmg`
files to `tar.gz` before submitting to the signing server. The signed binary
is a `tar.gz`.

`authenticode` signing takes individual binaries or a zipfile. We sign the
individual file or internals of the zipfile, skipping any already-signed files
and a select few blocklisted files (using the [should_sign_windows] function).
It returns a signed individual binary or zipfile with signed internals, depending
on the input. This format includes `gcp_prod_autograph_authenticode_202412`,
and `gcp_prod_autograph_authenticode_202412_stub`.

`mar` signing signs our update files (Mozilla ARchive). `mar_sha384` is
the same, but with a different hashing algorithm.

`gcp_prod_autograph_widevine` is also video-related; see the
[widevine site]. We sign specific files inside the package and rebuild the
`precomplete` file that we use for updates.

## Cert levels

Cert levels are how we separate signing privileges. We have the following levels:

`dep` is short for `depend`, which is a term from the Netscape days. (This
refers to builds that don't clobber, so they keep their dependency object files
cached from the previous build.) These certs and keys are designed to be used
for Try or on-push builds that we don't intend to ship. Many of these are
self-signed and not of high security value; they're intended for testing
purposes.

`nightly` refers to the Nightly product and channel. We use these keys for
signing and shipping nightly builds, as well as Devedition on the beta channel.
Because these are shipping keys, they are restricted; only a subset of branches
can request the use of these keys.

`release` refers to our releases, off the beta, release, or esr channels.
These are the most restricted keys.

We request a certain cert level via scopes:
`project:releng:signing:cert:dep-signing`,
`project:releng:signing:cert:nightly-signing`, or
`project:releng:signing:cert:release-signing`. Each signing task is required
to have exactly one of those scopes, and only nightly- and release-enabled
branches are able to use the latter two scopes. If a task is scheduled with one
of those restricted scopes on a non-allowlisted branch, Chain of Trust
verification will raise an exception.

## Signing scriptworker workerTypes

The [linux-depsigning] pool handles all of the non-mac dep signing. These are
heavily in use on try and autoland, but also other branches. These verify
the [chain of trust] artifact but not its signature, and they don't have a
gpg key to sign their own chain of trust artifact. This is by design; the chain
of trust should and will break if a production scriptworker is downstream from
a depsigning worker.

The [linux-signing] pool is the production signing pool; it handles the
nightly- and release- signing requests. As such, it verifies the upstream
chain of trust and all signatures, and signs its chain of trust artifact.

The [linux-devsigning] pool is intended for signingscript and scriptworker
development use. Because it isn't used on any Firefox-developer-facing branch,
Mozilla Releng is able to make breaking changes on this pool without affecting
any other team.

Similarly, we have the [mac-depsigning] and [mac-signing] pools, which handle
CI and nightly/release signing, respectively.

[60.0]: https://archive.mozilla.org/pub/firefox/releases/60.0/
[addonscript]: https://github.com/mozilla-releng/addonscript/
[chain of trust]: https://scriptworker.readthedocs.io/en/latest/chain_of_trust.html
[code signing]: https://en.wikipedia.org/wiki/Code_signing
[encrypted media extensions]: https://hacks.mozilla.org/2014/05/reconciling-mozillas-mission-and-w3c-eme/
[linux-depsigning]: https://firefox-ci-tc.services.mozilla.com/provisioners/scriptworker-k8s/worker-types/gecko-t-signing
[linux-devsigning]: https://firefox-ci-tc.services.mozilla.com/provisioners/scriptworker-k8s/worker-types/gecko-t-signing-dev
[linux-signing]: https://firefox-ci-tc.services.mozilla.com/provisioners/scriptworker-k8s/worker-types/gecko-3-signing
[mac-depsigning]: https://firefox-ci-tc.services.mozilla.com/provisioners/scriptworker-prov-v1/worker-types/depsigning-mac-v1
[mac-signing]: https://firefox-ci-tc.services.mozilla.com/provisioners/scriptworker-prov-v1/worker-types/signing-mac-v1
[scriptworker]: https://github.com/mozilla-releng/scriptworker/
[should_sign_windows]: https://github.com/mozilla-releng/signingscript/blob/65cbb99ea53896fda9f4844e050a9695c762d24f/signingscript/sign.py#L369
[signing password files]: https://github.com/mozilla/build-puppet/tree/feff5e12ab70f2c060b29940464e77208c7f0ef2/modules/signing_scriptworker/templates
[signingscript]: https://github.com/mozilla-releng/signingscript/
[signtool]: https://github.com/mozilla-releng/signtool
[widevine site]: https://www.widevine.com/wv_drm.html
