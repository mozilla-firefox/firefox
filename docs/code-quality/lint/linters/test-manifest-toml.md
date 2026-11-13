# Test Manifest TOML

This linter verifies syntax for ManifestParser TOML files.

## Run Locally

This Test Manifest linter can be run using mach:

```{eval-rst}
.. parsed-literal::

    $ ./mach lint --linter test-manifest-toml <file paths>

```

## Configuration

The configuration excludes all non-ManifestParser TOML files (as well as
generated TOML manifests).

## Conditional Expressions

In order to make conditional expressions (e.g. **skip-if**, **run-if**)
consistent and machine readable it is important that the
variable comparisons come before flags and are in a specific
order (ranking). Not all comparisons are required, but when
they appear they should be in ranked order.

What to put in **run-if**: should specify platform(s) where this test *should* run. This can be very general, e.g. `os == 'linux'`.

What to put in **skip-if**: platforms where the test currently does not run and has a bug to resolve the issue (with as much detail on the buggy conditions as possible).

Expression ranks:

1. **os** == 'android' (or 'linux', 'mac', 'win')

2. **os_version** == '14' (see below: Platforms that are currently supported in CI)

3. **arch** == 'aarch64' (or 'x86', 'x86_64', deprecated 'armeabi-v7a')

4. **display** == 'wayland' (or 'x11' -- on linux only)

5. **buildapp** == 'browser'

6. **appname** == 'firefox' (or 'seamonkey', 'thunderbird')

7. **build_type** FLAG, one of: asan, ccov, debug, tsan, opt

8. **variant** FLAG

   - a11y_checks
   - condprof
   - e10s
   - emewmf
   - fission
   - headless
   - http2
   - http3
   - inc_origin_init
   - mda_gpu
   - msix
   - nogpu
   - nova
   - privateBrowsing
   - remote_async
   - snapshot
   - swgl
   - trainhop
   - vertical_tab
   - wmfme
   - xorigin

9. **other** FLAG

   - artifact
   - crashreporter
   - datareporting
   - devedition
   - early_beta_or_earlier
   - false (*deprecated*)
   - gecko_profiler
   - isolated_process
   - is_emulator
   - is_ubuntu (*deprecated*)
   - msix
   - nightly_build
   - release_or_beta
   - require_signing
   - sessionHistoryInParent
   - sync
   - true
   - updater
   - verify
   - verify_standalone

## Notes

Certain build types have special significance:

- `opt` implies => `!asan && !ccov && !debug && !tsan`
- Instead of `!debug` use separate conditions for `asan`, `ccov` (rarely used), `opt`, and `tsan`

## Errors Detected

- Invalid TOML

- Disabling a path by commenting out the section

- Conditional contains explicit ||

- Conditional is NOT an array

- Missing include file

- Invalid combinations of platform and build-types (not present in CI)

  > - `"os == 'linux' && os_version == '22.04' && asan"`
  > - `"os == 'linux' && os_version == '22.04' && tsan"`
  > - `"os == 'win' && tsan`

- Non canonically formed conditions

  > - superfluous expression parenthesis
  > - unknown variables, flags or values
  > - variables and flags out of canonical order

## Warnings Detected (fixable)

- Missing DEFAULT section
- Sections not in alphabetical order
- Section name not double quoted

## Non idiomatic manifest warnings (fixable)

- Using `processor` instead of `arch`
- Using `bits` instead of `arch`
- Using `android_version` instead of `os_version`
- Using platform combination variables: `apple_catalina`, `apple_silicon`, `win10_2009`, `win11_2009` (unused)
- Platforms no longer used by CI: `Linux 18.04` (not enforced -- will be deprecated soon), `MacOS 11.20`, `Windows 11.2009`
- Not specifying display on Linux
- Specifying display `x11` on Linux 22.04 where only `wayland` is supported
- Specifying display `wayland` on Linux 24.04 where only `x11` is supported
- Using `!debug` instead of `asan`, `opt`, or `tsan`
- Using literal boolean values for single variables like `debug == false`

## Platforms that are currently supported in CI

- `os` one of `android`, `linux`, `mac`, `win`

- `os_version` depends on os

  > - `os == 'android'`
  >
  >   > - `os_version == '14'` arch: aarch64, x86_64
  >
  > - `os == 'linux'`
  >
  >   > - `os_version == '22.04'` arch: aarch64, x86_64
  >   > - `os_version == '24.04'` arch: aarch64, x86_64
  >
  > - `os == 'mac'`
  >
  >   > - `os_version == '10.15'` arch: x86_64
  >   > - `os_version == '14.70'` arch: x86_64
  >   > - `os_version == '15.30'` arch: aarch64
  >
  > - `os == 'win'`
  >
  >   > - `os_version == '10.2009'` arch: x86_64
  >   > - `os_version == '10.26100'` arch: aarch64, x86, x86_64

- `display` required for linux

  > - `os == 'linux'`
  >
  >   > - `os_version == '22.04'` display: wayland
  >   > - `os_version == '24.04'` display: x11

## Sources

- {searchfox}`Configuration (YAML) <tools/lint/test-manifest-toml.yml>`
- {searchfox}`Source <tools/lint/test-manifest-toml/__init__.py>`

## Developer Information

- The linter relies on code in: `testing/mozbase/manifestparser/manifestparser/`
  : - In particular the "legal" values for variables are defined in: `token.py`
- The linter self-test can be run with: `./mach python-test --subsuite mozlint tools/lint/test/test_manifest_toml.py`
