# Releasing geckodriver

## Standard Release Process

Releasing geckodriver is not as easy as it once used to be when the
project’s canonical home was on GitHub.  Today geckodriver is hosted
in the [Firefox repository], and whilst we do want to make future releases
from [Mozilla’s CI infrastructure], we are currently in between two
worlds: development happens in m-c, but releases continue to be made
from GitHub.

A [releasing script] automates most of the steps below to limit potential
manual failures. It has subcommands for each individual step — run
`--help` for usage details.

The steps to release geckodriver are as follows:

[Firefox repository]: https://github.com/mozilla-firefox/firefox
[Mozilla’s CI infrastructure]: https://treeherder.mozilla.org/
[releasing script]: https://bugzilla.mozilla.org/show_bug.cgi?id=1911056

### Update in-tree dependency crates

geckodriver depends on a number of Rust crates that also live in
central by using relative paths. Here an excerpt from its `Cargo.toml`:

```ini
[dependencies]
…
marionette = { path = "./marionette" }
…
mozdevice = { path = "../mozbase/rust/mozdevice" }
mozprofile = { path = "../mozbase/rust/mozprofile" }
mozrunner = { path = "../mozbase/rust/mozrunner" }
mozversion = { path = "../mozbase/rust/mozversion" }
…
webdriver = { path = "../webdriver" }
```

Because we need to export the geckodriver source code to the old
GitHub repository when we release, we first need to publish these
crates in the specified order if they have had any changes in the
interim since the last release.  If they have received no changes,
you can skip them:

- `testing/mozbase/rust/mozdevice`
- `testing/mozbase/rust/mozprofile`
- `testing/mozbase/rust/mozrunner`
- `testing/mozbase/rust/mozversion`
- `testing/webdriver`
- `testing/geckodriver/marionette`

For each crate:

1. Change into the crates folder.
2. Bump the version number in `Cargo.toml` based on [semantic versioning rules],
   and also update the version dependency for other in-tree crates using the
   currently modified crate. Note that running `cargo update` will fail if you
   missed updating a crate's dependency.

3. Use the [cargo-semver-checks] command to validate the version change:

    ```shell
    % cargo semver-checks check-release
    ```

4. Update dependencies:

   ```shell
   % cargo update -p <crate name>
   ```

5. We also publish audit information for the crates based on Mozilla's
   [audit criteria]. Because we use [wildcard audit entries] make sure that the
   latest day of publication is still within the `end` date. The related entry
   of the crate can be found at the top of [audits.toml]. If the date is over,
   then update its value to at most 1 year in the future.

6. Commit the changes for the modified [Cargo.toml] files, [Cargo.lock] and
   [audits.toml].

   ```shell
   % git add Cargo.toml Cargo.lock audits.toml testing
   % git commit -m "Bug XYZ - [rust-<name>] Release version <version>"
   ```

[semantic versioning rules]: https://semver.org/
[cargo-semver-checks]: https://crates.io/crates/cargo-semver-checks
[audit criteria]: https://mozilla.github.io/cargo-vet/audit-criteria.html
[wildcard audit entries]: https://mozilla.github.io/cargo-vet/wildcard-audit-entries.html
[Cargo.toml]: https://searchfox.org/firefox-main/source/testing/geckodriver/Cargo.toml
[Cargo.lock]: https://searchfox.org/firefox-main/source/Cargo.lock
[audits.toml]: https://searchfox.org/firefox-main/source/supply-chain/audits.toml

### Update the change log

Notable changes to geckodriver are mentioned in [CHANGES.md]. Many
users rely on this, so it’s important that you make it **relevant
to end-users**.  For example, we only mention changes that are visible
to users.  The change log is not a complete anthology of commits,
as these often will not convey the essence of a change to end-users.
If a feature was added but removed before release, there is no reason
to list it as a change.

It is good practice to also include relevant information from the
[webdriver], [marionette], [rust-mozrunner], and [rust-mozdevice] crates,
since these are the most important dependencies of geckodriver and a lot
of its functionality is implemented there.

To get a list of all the changes for one of the above crates the following
command can be used:

```shell
% git log --reverse <revision>..HEAD --pretty="%s" <path>
```

where `<revision>` is the changeset of the last geckodriver release and `<path>`
the location of the crate in the repository.

Add the list of changes to the related release bug on Bugzilla, and also check the
dependency list of the bug for other fixes that are worth mentioning.

We follow the writing style of the existing change log, with
one section per version (with a release date), with subsections
‘Added’, ‘Changed’, 'Fixed' and ‘Removed’.  If the targeted
Firefox or Selenium versions have changed, it is good to make a
mention of this.  Lines are optimally formatted at roughly 72 columns
to make the file readable in a text editor as well as rendered HTML.
fmt(1) does a splendid job at text formatting.

[CHANGES.md]: https://searchfox.org/firefox-main/source/testing/geckodriver/CHANGES.md
[webdriver]: https://searchfox.org/firefox-main/source/testing/webdriver
[marionette]: https://searchfox.org/firefox-main/source/testing/geckodriver/marionette
[rust-mozrunner]: https://searchfox.org/firefox-main/source/testing/mozbase/rust/mozrunner
[rust-mozdevice]: https://searchfox.org/firefox-main/source/testing/mozbase/rust/mozdevice

### Bump the version number and update the support page

Bump the version number in [Cargo.toml] to the next version.
geckodriver follows [semantic versioning] so it’s a good idea to
familiarise yourself with that before deciding on the version number.

After you’ve changed the version number, run

```shell
% ./mach build testing/geckodriver
```

again to update [Cargo.lock].

Now update the [support page] by adding a new row to the versions table,
including the required versions of Selenium, and Firefox.

Finally commit all those changes.

[semantic versioning]: http://semver.org/
[support page]: https://searchfox.org/firefox-main/source/testing/geckodriver/doc/Support.md

### Add the changeset id

To easily allow a release build of geckodriver after cloning the
repository, the changeset id for the release has to be added to the
change log. Therefore add a final place-holder commit to the patch
series, to already get review for.

Once all previous revisions of the patch series have been landed, and got merged
to `mozilla-central`, the changeset id from the merge commit has to picked for
finalizing the change log. This specific id is needed because Taskcluster creates
the final signed builds based on that merge.

### Release new in-tree dependency crates

Make sure to wait until the complete patch series from above has been
merged to the `main` branch. Then continue with the following steps.

Before releasing geckodriver all dependency crates as
[updated earlier](#update-in-tree-dependency-crates) have to be
released first.

Therefore change into each of the directories for crates with an update
and run the following command to publish the crate:

```shell
% cargo publish
```

Note that if a crate has an in-tree dependency make sure to first
change the dependency information.

Do not release the geckodriver crate yet!

Once all crates have been published observe the `/target/package/` folder under
the root of the Firefox repository and remove all the folders related
to the above published crates (it will save ~1GB disk space).

### Export to GitHub

The canonical GitHub repository is <https://github.com/mozilla/geckodriver.git>
so make sure you have a local clone of that.  It has three branches:
_master_ which only contains the [README.md]; _old_ which was the
state of the project when it was exported to the Firefox repository; and
_release_, from where releases are made.

Before we copy the code over to the GitHub repository we need to
check out the [release commit that bumped the version number](#add-the-changeset-id)
on `main` (formerly `mozilla-central`):

```shell
% git checkout $RELEASE_REVISION
```

We will now export the contents of [testing/geckodriver] to a new branch that
is based on the _release_ branch, which will be used to create a pull request:

```shell
% cd $SRC/geckodriver
% git checkout release
% git pull
% git checkout -b do_release_X.Y.Z
% git rm -rf .
% git clean -fxd
% cp -rt $SRC/gecko/testing/geckodriver .
```

Now verify that geckodriver builds correctly by running:

```shell
% cargo build
```

[README.md]: https://searchfox.org/firefox-main/source/testing/geckodriver/README.md
[testing/geckodriver]: https://searchfox.org/firefox-main/source/testing/geckodriver

### Commit local changes

Now commit all the changes you have made locally to the _release_ branch.
It is recommended to setup a [GPG key] for signing the commit, so
that the release commit is marked as `verified`.

```shell
% git add . -- ':!mach_commands.py :!moz.build :!target/*'
% git commit -S -am "Import of vX.Y.Z" (signed)
```

or if you cannot use signing use:

```shell
% git add . -- ':!mach_commands.py :!moz.build :!target/*'
% git commit -am "Import of vX.Y.Z" (unsigned)
```

Then push the changes, and create a pull request:

```shell
% git push origin do_release_X.Y.Z
```

As indicated above, the changes you make to this branch will not
be upstreamed back into the Firefox repository.  It is merely used as a
place for external consumers to build their own version of geckodriver.

[GPG key]: https://help.github.com/articles/signing-commits/

### Make the release

geckodriver needs to be manually released on github.com. Therefore start to
[draft a new release], and make the following changes:

1. Specify the "Tag version", and select "Release" as target.

2. Leave the release title empty

3. Paste the raw Markdown source from [CHANGES.md] into the description field.
   This will highlight for end-users what changes were made in that particular
   package when they visit the GitHub downloads section. Make sure to check that
   all references can be resolved, and if not make sure to add those too.

4. Find the signed geckodriver archives in the [taskcluster index] by
   replacing %changeset% with the full release changeset id. Rename the
   individual files so the basename looks like 'geckodriver-v%version%-%platform%'.
   For macOS use the `macosx64-notarized` builds.

   The macOS build is a universal binary (x86_64 + aarch64). For
   backward compatibility, create a copy of the macOS archive with
   the `-aarch64` platform suffix so that existing consumers that
   reference the architecture-specific filename continue to work.

   Upload them all, including the checksum files for the Linux platforms.

5. Before announcing the release on GitHub publish the geckodriver crate as well
   on crates.io by running `cargo publish` from the release branch.

6. Send the release announcement to the [dev-webdriver] mailing list.

[draft a new release]: https://github.com/mozilla/geckodriver/releases/new
[taskcluster index]: https://firefox-ci-tc.services.mozilla.com/tasks/index/gecko.v2.mozilla-central.revision.%changeset%.geckodriver
[dev-webdriver]: https://groups.google.com/a/mozilla.org/g/dev-webdriver

Congratulations!  You’ve released geckodriver!

## Out-of-Band WebDriver Releases

Occasionally, an out-of-band release of the `webdriver` crate may be required —
for example, to fix a newly introduced regression that affects external WebDriver
clients, while not necessitating a full geckodriver release.

When preparing such a release, be especially cautious when selecting the
semantic version number, to avoid breaking users who install geckodriver via
`cargo install`.

### Bump the version number

To publish a fix for the `webdriver` crate, you’ll need to update its version.
Decide whether the change qualifies as a bugfix or a minor release:

1. Review recent changes in all geckodriver dependent crates since the last
   geckodriver release.

2. If external dependencies have been updated in any of the other crates, check
   whether they could cause version conflicts with geckodriver by comparing
   them against the upstream geckodriver repository on the `release` branch.
   If any conflicts exist, bump the minor version. Otherwise, increment the
   patch version for a bugfix release.

In detail follow these steps:

1. Change into the `testing/webdriver` folder.
2. Update the version number in Cargo.toml based on the assessment above,
   following [semantic versioning rules].

     - For bugfix releases, there is no need to update the geckodriver crate.
     - For minor releases, update the dependency in the geckodriver crate as well.
     - Note: cargo update will fail if a dependency update is missed.

3. Use the [cargo-semver-checks] command to validate the version change:

    ```shell
    % cargo semver-checks check-release
    ```

4. Update dependencies:

   ```shell
   % cargo update -p <crate name>
   ```

5. We also publish audit information for the crates based on Mozilla's
   [audit criteria]. Because we use [wildcard audit entries] make sure that the
   latest day of publication is still within the `end` date. The related entry
   of the crate can be found at the top of [audits.toml]. If the date is over,
   then update its value to at most 1 year in the future.

6. Commit the changes for the modified [Cargo.toml] files, [Cargo.lock] and
   [audits.toml].

   ```shell
   % git add Cargo.toml Cargo.lock audits.toml testing
   % git commit -m "Bug XYZ - [rust-webdriver] Release version <version>"
   ```

### Release the new webdriver crate

Make sure to wait until the complete patch series from above has been
merged to the `main` branch. Then continue with the following steps.

To release the webdriver crate change into `testing/webdriver` and run the
following command to publish the crate:

```shell
% cargo publish
```

Once published observe the `/target/package/` folder under
the root of the Firefox repository and remove all the folders related
to the webdriver crate.

### Verify the release

To confirm the release was successful, verify that geckodriver can still be
installed using:

```shell
cargo install geckodriver
```

If the installation fails:

- Immediately yank the published `webdriver` crate from crates.io by using:

    ```shell
    cargo yank --version <version> webdriver
    ```

- Perform a minor version bump, and repeat the release process.
