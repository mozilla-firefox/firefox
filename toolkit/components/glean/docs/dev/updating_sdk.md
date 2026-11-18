# Updating the Glean SDK

Project FOG uses the Glean SDK published as the [`glean`][glean-crate]
and [`glean-core`][glean-core] crates on crates.io.

[glean-crate]: https://crates.io/crates/glean
[glean-core]: https://crates.io/crates/glean-core

These two crates' versions are included in several places in the Firefox repository.
To update them all, you should use the command
`mach update-glean <version, like "54.1.0">`.

This is a semi-manual process.
Please pay attention to the output of `mach update-glean` for instructions,
and follow them closely.

## Version mismatches of Rust dependencies

Other crates that are already vendored might require a different version of the same dependencies that the Glean SDK requires.
The general strategy for Rust dependencies is to keep one single version of the dependency in-tree
(see [comment #8 in bug 1591555](https://bugzilla.mozilla.org/show_bug.cgi?id=1591555#c8)).
This might be hard to do in reality since some dependencies might require tweaks in order to work.
The following strategy can be followed to decide on version mismatches:

* If the versions only **differ by the patch version**, Cargo will keep the vendored version,
  unless some other dependency pinned specific patch versions;
  assuming it doesn’t break the Glean SDK;
  if it does, follow the next steps;
* If the version of the **vendored dependency is newer** (greater major or minor version) than the version required by the Glean SDK,
  [file a bug in the Glean SDK component][glean-bug] to get Glean to require the same version;
    * You will have to abandon updating the Glean SDK to this version.
      You will have to wait for Glean SDK to update its dependency and for a new Glean SDK release.
      Then you will have to update to that new Glean SDK version.
* If the version of the **vendored dependency is older** (lower major or minor version), consider updating the vendored version to the newer one;
  seek review from the person who vendored that dependency in the first place;
  if that is not possible or breaks the Firefox build, then consider keeping both versions vendored in-tree; please note that this option will probably only be approved for small crates,
  and will require updating the `TOLERATED_DUPES` list in `mach vendor`
  (instructions are provided as you go).

## Keeping versions in sync

The Glean SDK and `glean_parser` are currently released as separate projects.
However each Glean SDK release requires a specific `glean_parser` version.
When updating one or the other ensure versions stay compatible.
You can find the currently used `glean_parser` version in the Glean SDK source tree, e.g. in [sdk_generator.sh].

In most cases you should update `glean_parser` first before updating the SDK.

## Special Concerns for Major Version Updates

If you are updating the major version of the Glean SDK
(ie from '32.x.y' to '33.z.a')
then there are specific steps to take.
The `application-services` repository integrates the Glean SDK separately.
It accepts any version within a major release,
so for non-major-version updates, it happily follows `mozilla-central`'s lead.
When there's a major version update,
we need to update `application-services` first,
allow it to generate a new (nightly) release,
and ensure that `mozilla-central` picks up that release at the same time it switches over to the new Glean SDK version.

To update the Glean SDK version used by `application-services`:
1) Consider not doing it yourself if someone else on the team who has done it before has the time.
   [Setting yourself up to build and test `application-services`][as-contributing]
   is straightforward, but non-trivial.
   You may find it easier to ask them to do it.
2) Update Gradle's version of the Glean dependency for Android support:
    * Edit `gradle/libs.versions.toml` to specify e.g. `glean = "62.0.0"`
3) Update the `glean-swift` version used:
    * Edit `megazords/ios-rust/Package.swift` to specify the correct version for `https://github.com/mozilla/glean-swift`
    * Xcode also has a lockfile. If you do not have Xcode, or just don't want to run it,
      edit `megazords/ios-rust/Package.resolved` to specify the new version plus the git SHA of the new release of [mozilla/glean-swift][glean-swift]
      e.g. `"revision" : "5c614b4af5a1f1ffe23b46bd03696086d8ce9d0d",` and `"version": "62.0.0"`
4) Run `cargo test` to make sure nothing obvious is broken.
5) Submit your PR for review and have it merged.
6) Make your `mozilla-central` changes depend on the patch the auto-update bot created to update
   `mozilla-central` to the new `application-services` version that contains your changes.
    * This will happen if you wait a day. Look for
      an automatically-filed bug
      ([e.g.](https://bugzilla.mozilla.org/show_bug.cgi?id=1893248)).
    * If you need to operate faster, you can ask `application-services`
      folks to trigger a build for you, then write the patch yourself.
7) Push the whole stack to `try` to make sure the tree works together.
8) When everything's green, supply the whole stack to Lando to land it.

[sdk_generator.sh]: https://github.com/mozilla/glean/blob/main/glean-core/ios/sdk_generator.sh#L28
[glean-bug]: https://bugzilla.mozilla.org/enter_bug.cgi?product=Data+Platform+and+Tools&component=Glean%3A+SDK&priority=P3&status_whiteboard=%5Btelemetry%3Aglean-rs%3Am%3F%5D
[application-services]: https://github.com/mozilla/application-services
[as-contributing]: https://github.com/mozilla/application-services/blob/main/docs/contributing.md
[glean-swift]: https://github.com/mozilla/glean-swift
