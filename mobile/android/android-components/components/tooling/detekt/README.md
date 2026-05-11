# [Android Components](../../../README.md) > Tooling > Detekt

Custom Detekt rules for the components repository.

These additional detekt rules are run as part of the _Android Components_ build pipeline.
Published for internal usage only.

## Usage

Add into `build.gradle`:
```
dependencies {
    // ...

    detektPlugins "org.mozilla.components:tooling-detekt:$android_components_version"
}
```

## Rules

Section `mozilla-rules`:

  - `AbsentOrWrongFileLicense` - check for correct license header in Kotlin files.

Section `acorn-detekt-rules`:

  - `HardcodedPaddingUsage` - check for usage of Acorn spacing tokens for padding in Kotlin files.

## License

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/
