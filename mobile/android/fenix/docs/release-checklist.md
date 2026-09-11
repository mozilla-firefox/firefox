---
layout: page
title: Release checklist
permalink: /contributing/release-checklist
---

# Release Checklist

These are instructions for preparing a release branch for Firefox Android and starting the next Nightly development cycle.

## [Release Engineering / Release Management] Beta release branch creation & updates

**This part is covered by the Release Engineering and Release Management teams. The dev team should not perform these steps.**

1. Release Management emails the release-drivers list to begin merge day activities. For example, `Please merge mozilla-central to mozilla-beta, bump central to 123.` A message will also be sent to the #releaseduty Matrix channel.
2. The Release Engineering engineer on duty (“relduty”) triggers the Taskcluster merge day hooks. The hooks automate the following tasks:
   - Migrating tip of mozilla-central to mozilla-beta
   - Updating version.txt on mozilla-central for the new Nightly version
   - Updating version.txt on mozilla-beta to change the version string from `[beta_version].0a1` to `[beta_version].0b1`
3. When the merge day tasks are completed, relduty will respond to the release-drivers thread and #releaseduty channel that merges are completed. This also serves as notification to the dev team that they can start the new Nightly development cycle per the steps given in the next section ⬇️
4. In [ApplicationServices.kt](https://hg.mozilla.org/mozilla-central/file/default/mobile/android/android-components/plugins/dependencies/src/main/java/ApplicationServices.kt):
   - Set `VERSION` to `[major-version].0`
   - Set `CHANNEL` to `ApplicationServicesChannel.RELEASE`

     ```diff
     --- a/mobile/android/android-components/plugins/dependencies/src/main/java/ApplicationServices.kt
     +++ b/mobile/android/android-components/plugins/dependencies/src/main/java/ApplicationServices.kt
     -val VERSION = "121.20231118050255"
     -val CHANNEL = ApplicationServicesChannel.NIGHTLY
     +val VERSION = "121.0"
     +val CHANNEL = ApplicationServicesChannel.RELEASE
     ```

   - Create a commit named `Switch to Application Services Release`. (Note: application-services releases directly after the nightly cycle, there's no beta cycle).
5. Once all of the above commits have landed, create a new `Firefox Android (Android-Components, Fenix, Focus)` release in [Ship-It](https://shipit.mozilla-releng.net/) and continue with the release checklist per normal practice.

## [Dev team] Starting the next Nightly development cycle

**Please handle this part once Release Management gives you the go.**

Now that we made the Beta cut, we can remove all the unused strings marked moz:removedIn <= `[EXPIRED_STRING_VERSION]`.
If `CURRENT_VERSION` is the nightly version number _before_ the Beta cut, then `EXPIRED_STRING_VERSION = CURRENT_VERSION - 2`.
It corresponds to the `RELEASE_VERSION` number _before_ the release of the new version.

We will also want to bump the Android Component's [changelog.md](https://hg.mozilla.org/mozilla-central/file/default/mobile/android/android-components/docs/changelog.md) with the new Nightly development section.

0. Wait for Release teams to do the merge (see #3 above). To find out if the merge has been done yet, you can check the [version number on the main branch](https://github.com/mozilla-firefox/firefox/blob/main/browser/config/version_display.txt) to see if it has updated to the next version.
1. File a Bugzilla issue named "Start the Nightly `[nightly_version]` development cycle". ([Example here](https://bugzilla.mozilla.org/show_bug.cgi?id=2004705))
2. Run `./mobile/android/beta-cut.py BUG_ID`, with `BUG_ID` being the id of the bug you created at step 1. This will:
   - Update the [changelog.md](https://hg.mozilla.org/mozilla-central/file/default/mobile/android/android-components/docs/changelog.md)
   - Search and remove all strings marked `moz:removedIn="[EXPIRED_STRING_VERSION]"` across Fenix, Focus and Android Components, limit changes only to `values/strings.xml` (the localized `strings.xml` should not be changed).
   - Create a commit using the BUG_ID you provided, and describing all the changes that were applied.
   - Add SERP telemetry json to the [search-telemetry-v2.json](https://hg.mozilla.org/mozilla-central/file/default/mobile/android/android-components/components/feature/search/src/main/assets/search/search_telemetry_v2.json), copying the content from the desktop telemetry dump located at [desktop-search-telemetry-v2.json](https://searchfox.org/firefox-main/source/services/settings/dumps/main/search-telemetry-v2.json)

Please read carefully the output of the script and follow the instructions it provides.
More specifically, you will need to:

- Remove the remaining uses of the removed strings and amend the commit, if the output shows remaining uses of removed strings. (⚠️ This output is purely indicative and might show false positives, see more details bellow)
- Review the changes and make sure they are correct (Some header comments might become useless after the strings removal)
- Submit the patch and push to `try`
- Please also check if the strings that were removed are used in [Reference Browser](https://github.com/mozilla-mobile/reference-browser), and open a PR to remove them if it is the case.

### More details on removing remaining uses of the removed strings

`beta-cut.py` will try to show you potential remaining usages of the removed strings, but keep in mind that it is purely indicative and might show false positives. Please check if they are legitimate before removing the corresponding uses.
For example, if the string `xyz` was removed from Fenix, but another `xyz` string exists in Android Components, the output will suggest to remove the uses of `xyz` in Android Components although they shouldn't be removed.

### Ask for Help

- Issues related to releases `#releaseduty` on Element
- Topics about CI (and the way we use Taskcluster) `#Firefox CI` on Element
- Breakage in PRs due to Gradle issues, usage of `beta-cut.py` or GV upgrade problems `#mobile-android-team` on Slack
