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
4. In [ApplicationServices.kt](https://searchfox.org/firefox-main/source/mobile/android/android-components/plugins/dependencies/src/main/java/ApplicationServices.kt):
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

## [Automated] Starting the next Nightly development cycle

The automation will bump Android Component's [changelog.md](https://searchfox.org/firefox-main/source/mobile/android/android-components/docs/changelog.md) with the new Nightly development section.

### Ask for Help

- Issues related to releases `#releaseduty` on Element
- Topics about CI (and the way we use Taskcluster) `#Firefox CI` on Element
- Breakage in PRs due to Gradle issues or GV upgrade problems `#mobile-android-team` on Slack
