# Contributing code to Mozilla's Android projects

Thank you for taking the time to contribute to one of Mozilla's Android
projects! 🔥 🦊 ❤️ 🤖! 🎉 👍 For a full list of projects, see
[the README](/mobile/android/index.md).

Before contributing, please review our [Community Participation Guidelines].

If you run into trouble at any point, ask for help! Check out our
[preferred communication channels.](./CONTRIBUTING.md#communication)

Contents:

- [Beginner's guides](#beginners-guides)
- [Configuring Android Studio](#configuring-android-studio)
- [Finding issues to work on](#finding-issues-to-work-on)
- [Submitting a patch](#submitting-a-patch)
- [Writing tests](#writing-tests)

## Beginner's guides

Unfamiliar with the technology we use? No problem! We were once new to this
too! Here are few guides we've compiled to help you get started:

- [Git guide](../git_guide.md)
- [Android guide](android_guide.md)
  - [Accessibility guide](accessibility_guide.md)
- [Kotlin guide](kotlin_guide.md)
- [Writing custom lint rules guide](writing_lint_rules.md)
- [Taskcluster guide](taskcluster_guide.md)
- [MockK guide](https://notwoods.github.io/mockk-guidebook/)

If these are confusing or if you have questions, please let us know!

## Configuring Android Studio

We don't allow wildcard imports (ex: `import kotlinx.coroutines.*`) so we recommend preventing Android Studio from auto-importing or optimizing with them. This can be done via the `Preferences > Editor > Kotlin > Code Style > Use single name import` option.

## Finding issues to work on

See {ref}`good-first-bug-guide` for how to find a bug and claim it; good first bugs for
[Fenix](https://codetribute.mozilla.org/projects/fenix) and
[Focus](https://codetribute.mozilla.org/projects/focus) are listed on Codetribute.

**If you want to work on a new feature**, *always file a bug first* and wait
for our team to discuss it. We want to ensure all teams (product, ux, engineering)
have an opportunity to provide feedback. **Patches for unsolicited features
are unlikely to get merged.**

## Submitting a patch

The Android projects live in this repository and follow the same process as the
rest of Firefox: see the {ref}`Firefox Contributors' Quick Reference
<firefox-contributors-quick-reference>` for writing, testing and submitting a patch, and
{doc}`/contributing/reviews` for requesting review (Android front-end patches
usually go to `#android-reviewers`). If there are UI changes, include a screenshot so
UX can also do a visual review.

## Writing tests

To learn more about how our tests are structured, see [testing.md](testing.md).

[Community Participation Guidelines]: https://www.mozilla.org/en-US/about/governance/policies/participation/
