# Balrog in Release Promotion

## Overview

Balrog is Mozilla's update server. It is responsible for delivering newer versions of Firefox to existing Firefox installations. If you are not already, it would be useful to be familiar with Balrog's core concepts before continuing with this doc. You can find that information on [Balrog's official documentation].

The basic interactions that Release Promotion has with Balrog are as follows:

1. Submit new release metadata to Balrog with a number of `balrog` tasks and the `release-balrog-submit-toplevel` task.
2. Update test channels to point at the new release in the `release-balrog-submit-toplevel` task.
3. Verify the new release updates with `release-final-verify` tasks.
4. Schedule the new release to be shipped in the `release-balrog-scheduling` task.

## Submit New Release Metadata

Balrog requires many different pieces of information before it can ship updates. Most of this information revolves around update file (MAR) metadata (hash, filesize, target platform, target locale). This information is submitted by `balrog` tasks.

We also submit some more general information about releases (version number, MAR url templates, release name, etc.) as part of the `release-balrog-submit-toplevel` task.

All balrog submission is done by [balrogscript workers], and happens in the `promote` phase.

### WNP and Release Notes URL Configuration

The What's New Page (WNP) and release notes URLs submitted to Balrog are
configured in `browser/config/whats_new_page.yml`. See the
[relengdocs explanation](https://mozilla-releng.net/relengdocs/explanations/balrog/wnp-release-notes-urls.html)
for full details.

## Update Test Channels

Balrog has "test" channels that we use to allow verification of new release updates prior to shipping. The `release-balrog-submit-toplevel` task is responsible for updating these test channels whenever we prepare a new release. This happens in the `promote` phase.

## Verify the Release

After a release has been pushed to cdns, `release-final-verify` runs and performs some additional checks. This happens in the `push` phase.

## Schedule Shipping

When we're ready to ship a release we need to let Balrog know about it by scheduling a change to the appropriate Balrog rule. If `release_eta` is set it will be used as the ship date and time. If not, the release will be scheduled for shipping 5 minutes in the future. In either case, signoff will need to be done in Balrog by multiple parties before the release is actually made live.

This step is done by the `release-balrog-scheduling` task in the `ship` phase.

[balrog's official documentation]: http://mozilla-balrog.readthedocs.io/en/latest/
[balrogscript workers]: https://github.com/mozilla-releng/balrogscript
