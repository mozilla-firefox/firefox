---
name: android
description: Workflow guide when working with Android builds or the mobile/ directory.
---

# Android

## Workflow
- Instead of `gradlew`, use `./mach gradle` as the wrapper. Use `-p` argument of gradle if you need to run in a subdirectory

## Android Frontend
When working in `mobile/android/fenix`, `mobile/android/focus-android` or `mobile/android/android-components` directories override instructions from the top-level `AGENTS.md` with:
- `./mach lint` is the recommended way to run linters and format code all in one go.
    - It is currently too slow to run in fast feedback loops. Run it only at the end of implementing a change (or a major piece of a bigger change), like when checking if it's ready to submit a patch.
    - In case you need a faster feedback loop, you can run a subset of fast linters with `./mach gradle ktfmtFormat detekt`. It is not equivalent to `./mach lint` and doesn't guarantee patch submission readiness.
    - Default to running code formatting as part of `./mach lint` or with `./mach gradle ktfmtFormat`. Don't run `./mach format` in fenix/focus-android/android-components, because it skips android formatters.
- Run `./mach build` only when new changes are pulled into the work tree, or if `./mach gradle ...` commands do not succeed.
- Run `./mach gradle [fenix|focus|components:<component>]:assembleDebug` when you want to verify subsequent Android frontend code changes.
- Don't run `./mach test` with `--auto`, it doesn't work on Android frontend code changes. Instead, run it for a file/feature you're working on with:
    - `./mach test <file-name>` to run all tests in the file
    - `./mach test <directory-name>` to run all tests in the directory
    - `./mach test {fenix,focus,ac,android-components,geckoview}` to run all tests in the specific project
