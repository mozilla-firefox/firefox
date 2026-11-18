# Contributing

## Raptor on Mobile projects (Fenix, Reference-Browser)

### Add new tests

For mobile projects, Raptor tests are on the following repositories:

**Fenix**:

- Repository: {searchfox}`Mozilla Central <mobile/android/fenix>`
- Tests results: [Treeherder view](https://treeherder.mozilla.org/#/jobs?repo=fenix)
- Schedule: Every 24 hours [Taskcluster force hook](https://tools.taskcluster.net/hooks/project-releng/cron-task-mozilla-mobile-fenix%2Fraptor)

**Reference-Browser**:

- Repository: [Github](https://github.com/mozilla-mobile/reference-browser/)
- Tests results: [Treeherder view](https://treeherder.mozilla.org/#/jobs?repo=reference-browser)
- Schedule: On each push

Tests are now defined in a similar fashion compared to what exists in the Firefox repository. Task definitions are expressed in YAML:

- <https://github.com/mozilla-mobile/fenix/blob/1c9c5317eb33d92dde3293dfe6a857c279a7ab12/taskcluster/ci/raptor/kind.yml>
- <https://github.com/mozilla-mobile/reference-browser/blob/4560a83cb559d3d4d06383205a8bb76a44336704/taskcluster/ci/raptor/kind.yml>

If you want to test your changes on a PR, before they land, you need to apply a patch like this one: <https://github.com/mozilla-mobile/fenix/pull/5565/files>. Be sure to revert it before merging the patch. Note that the checks will run but the results aren't currently available on Treeherder ([bug 1593252](https://bugzilla.mozilla.org/show_bug.cgi?id=1593252) is expected to address this).

On Fenix and Reference-Browser, the Raptor revision is tied to the latest nightly of mozilla-central

For more information, please reach out to :mhentges in #cia

## Code formatting on Raptor

As Raptor is a Mozilla project we follow the general Python coding style:

- {doc}`/code-quality/coding-style/coding_style_python`

[black](https://github.com/psf/black/) is the tool used to reformat the Python code.
