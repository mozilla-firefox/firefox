# Updating glean_parser

Project FOG uses the `glean_parser` to generate code from metric definitions.
It depends on [glean-parser] from pypi.org

[glean-parser]: https://pypi.org/project/glean-parser/

To update the in-tree glean-parser change the version in `third_party/python/pyproject.toml`,
then run

```
./mach vendor python
```

We presently pin our version of glean-parser to the version in [sdk_generator.sh],
otherwise we could use some of the
[more interesting switches on ./mach vendor python][vendor-python].

[vendor-python]: /python/index

## Version mismatch of the Python dependencies

The logic for handling version mismatches is very similar to the one for the Rust crates.
See [Updating the Glean SDK](updating_sdk.md) for details.

## Keeping versions in sync

The Glean SDK and `glean_parser` are currently released as separate projects.
However each Glean SDK release requires a specific `glean_parser` version.
When updating one or the other ensure versions stay compatible.
You can find the currently used `glean_parser` version in the Glean SDK source tree, e.g. in [sdk_generator.sh].

[sdk_generator.sh]: https://github.com/mozilla/glean/blob/main/glean-core/ios/sdk_generator.sh#L28

## Using a local `glean_parser` development version

To test out a new `glean_parser` in Firefox, follow these steps:

1. Remove `glean_parser` from the user-wide virtual environment.
   This can be found in a path like `~/.mozbuild/srcdirs/gecko-f5e3b9c6ded5/_virtualenvs/mach/lib/python3.10/site-packages/glean_parser`
   Note that the `gecko-f5e3b9c6ded5` part will be different depending on your local checkout.
   Remove all directories and files mentioning `glean_parser`
2. Remove `glean_parser` from the build virtual enviromment.
   This can be found in `$MOZ_OBJDIR/_virtualenvs/common/lib/python3.6/site-packages/glean_parser`.
   Note that `$MOZ_OBJDIR` depends on your local mozconfig configuration.
   Remove all directories and files mentioning `glean_parser`
3. Copy the local `glean_parser` checkout into `third_party/python/glean_parser`.
   E.g. `cp ~/code/glean_parser $GECKO/third_party/python/glean_parser`.

You should now be able to build `mozilla-central` and it will use the modified `glean_parser`.
You can make further edits in `$GECKO/third_party/python/glean_parser`.
