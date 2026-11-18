# CondProf Addons

CondProf Addons is a linter for condprof customization JSON files (see {searchfox}`testing/condprofile/condprof/customization`),
it reports linting errors if:

- any of the addons required by the customization files (e.g. see {searchfox}`testing/condprofile/condprof/customization/webext.json`)
  is not found in the tar file fetched through the `firefox-addons` fetch task (see {searchfox}`taskcluster/kinds/fetch/browsertime.yml`)
- or the expected `firefox-addons` fetch task has not been found

## Run Locally

The mozlint integration of condprof-addons can be run using mach:

```{eval-rst}
.. parsed-literal::

    $ mach lint --linter condprof-addons <file paths>
```

Alternatively, if the `--linter condprof-addons` is omitted, the `condprof-addons` will still be selected automatically if
any of the files paths passed explicitly is detected to be part of the condprof customization directory.

The `condprof-addons` will also be running automatically on `mach lint --outgoing` if there are customization files changes
detected in the outgoing patches.

## Run on Taskcluster

The condprof-addons job shows up as `misc(condprof-addons)` in the linting job. It should run automatically if changes are made
to condprof customization JSON files.

## Fix reported errors

### XPI file is missing from the firefox-addons.tar archive

This linting errors is expected to be reported if the linter detected that a confprof customization file
requires an addon but the related xpi filename is not included in the firefox-addons.tar file fetched
through the `firefox-addons` fetch task (see {searchfox}`taskcluster/kinds/fetch/browsertime.yml`).

If the patch or phabricator revision is not meant to be landed, but only used as a temporary patch
pushed on try or only applied locally (e.g. to run the tp6/tp6m webextensions perftests with a given
third party extension installed to gather some metrics and/or GeckoProfiler data), then it can be
safely ignored.

On the contrary, if the patch or phabricator revision is meant to be landed in the Firefox repository,
the linting error have to be fixed before or along landing the change, either by:

- removing the addition to the customization file if it wasn't intended to include that addon to all runs
  of the tp6/tp6m webextensions perftests
- updating the `firefox-addons` fetch task as defined in {searchfox}`taskcluster/kinds/fetch/browsertime.yml`
  by creating a pull request in the github repository where the asset is stored, and ask a review from
  a peer of the `#webextensions-reviewer` and `#perftests-reviewers` review groups.

### firefox-addons taskcluster config 'add-prefix' attribute should be set to 'firefox-addons/'

If this linting error is hit, then the `firefox-addons` task defined in {searchfox}`taskcluster/kinds/fetch/browsertime.yml`
is missing the `add-prefix` attribute or its value is not set to the expected 'firefox-addons/' subdir name.

This is enforced as a linting rule because when the condprof utility is going to build a conditioned profile
for which some add-ons xpi files are expected to be sideloaded (see {searchfox}`testing/condprofile/condprof/customization.webext.json`),
to avoid re-downloading the same xpi from a remote urls every time the conditioned profile is built on the build infrastructure
(which for tp6/tp6m perftests will happen once per job being executed) condprof is going to look first if the expected xpi file
names are already available in `$MOZ_FETCHES_DIR/firefox-addons`.

### firefox-addons taskcluster fetch config section not found

This linting error is hit if the linter does not find the expected `firefox-addons` task defined in {searchfox}`taskcluster/kinds/fetch/browsertime.yml`
or it is missing the expected `fetch` attribute.

## Configuration

ConfProf Addons does not currently provide any configuration files.

## Sources

- {searchfox}`Configuration (YAML) <tools/lint/condprof-addons/yml>`
- {searchfox}`Source <tools/lint/condprof-addons/__init__.py>`
- {searchfox}`Test <tools/lint/test/test_condprof_addons.py>`
