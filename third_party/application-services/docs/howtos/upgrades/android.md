# Java

Our Java version should be kept in sync with the version Firefox Android is using.
Update the version number in:

- `docs/building.md`
- `libs/verify-android-ci-environment.sh`
- `taskcluster/docker/linux/Dockerfile`


# Android SDK (Software Development Kit)

The Android SDK is a set of development tools for building Android Apps.

## Compile version

This specifies what we use to build our code.
Keep this in sync with the version Firefox Android is using.
Update the version number in:

* `settings.gradle`
* `taskcluster/docker/linux/Dockerfile`

## Target / minimum versions

These control which devices the library is compatible with and control which version-specific features are enabled.
Keep this in sync with the version Firefox Android is using.
Update the version number in:

* `settings.gradle`

# Android NDK (Native Development Kit)

The Android NDK is the toolset we use for cross-compiling our Rust code so that Firefox Android can dynamically link to it.

* Update the version number in:
  * `settings.gradle` (search for `ndkVersion`)
  * `taskcluster/docker/linux/Dockerfile` (search for `ANDROID_NDK_VERSION`)
  * `components/support/rc_crypto/nss/nss_build_common/src/lib.rs` (search for `ANDROID_NDK_VERSION`)
* Update these docs by replacing the old version with the new one:
  * docs/building.md
  * docs/howtos/locally-building-jna.md
* These may need updating if the directory structure changed between this version and the last
  * `components/support/rc_crypto/nss/nss_build_common/src/lib.rs`: search for
    `DARWIN_X86_64_LIB_DIR` and `LINUX_X86_64_LIB_DIR`, and ensure that both point to the correct lib directory containing
    `libclang_rt.builtins-x86_64-android.a`.
