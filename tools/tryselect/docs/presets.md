# Presets

Some selectors, such as `fuzzy`, allow saving and loading presets from a file. This is a
good way to reuse a selection, either at a later date or by sharing with others. Look for a
'preset' section in `mach try <selector> --help` to determine whether the selector supports this
functionality.

## Using Presets

To save a preset, run:

```shell
mach try <selector> --save <name> <args>
```

For example, to save a preset that selects all Windows mochitests:

```shell
$ mach try fuzzy --save all-windows-mochitests --query "'win 'mochitest"
preset saved, run with: --preset=all-windows-mochitests
```

Then run that saved preset like this:

```shell
mach try --preset all-windows-mochitests
```

To see a list of all available presets run:

```shell
mach try --list-presets
```

## Editing and Sharing Presets

Presets can be defined in one of two places, in your home directory or in a file checked into
the Firefox repository.

### Local Presets

These are defined in your `$MOZBUILD_STATE_DIR`, typically `~/.mozbuild/try_presets.yml`.
Presets defined here are your own personal collection of presets. You can modify them by running:

```shell
./mach try --edit-presets
```

### Shared Presets

You can also check presets into the Firefox repository in {searchfox}`tools/tryselect/try_presets.yml`. These presets
will be available to all users of `mach try`, so please be mindful when editing this file. Make
sure the name of the preset is scoped appropriately (i.e doesn't contain any team or module specific
terminology). It is good practice to prefix the preset name with the name of the team or module that
will get the most use out of it.

### Preset Format

Presets are simple key/value objects, with the name as the key and a metadata object as the value.
For example, the preset saved above would look something like this in `try_presets.yml`:

```yaml
all-windows-mochitests:
    selector: fuzzy
    description: >-
        Runs all windows mochitests.
    query:
        - "'win 'mochitest"
```

The `selector` key (required) allows `mach try` to determine which subcommand to dispatch to.
The `description` key (optional in user presets but required for shared presets) is a human
readable string describing what the preset selects and when to use it. All other values in the
preset are forwarded to the specified selector as is.
