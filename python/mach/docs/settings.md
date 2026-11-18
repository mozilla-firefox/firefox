(mach-settings)=

# Mach settings

Mach can read settings in from a set of configuration files. These
configuration files are either named `machrc` or `.machrc` and
are specified by the bootstrap script. In the Firefox repository, these files
can live in `~/.mozbuild` and/or `topsrcdir`.

Settings can be specified anywhere, and used both by mach core or
individual commands.

## Core Settings

These settings are implemented by mach core.

- alias - Create a command alias. This is useful if you want to alias a command to something else, optionally including some defaults. It can either be used to create an entire new command, or provide defaults for an existing one. For example:

```{eval-rst}
.. parsed-literal::

    [alias]
    mochitest = mochitest -f browser
    browser-test = mochitest -f browser

```

## Defining Settings

Settings need to be explicitly defined, along with their type,
otherwise mach will throw when trying to access them.

To define settings, use the {func}`~decorators.SettingsProvider`
decorator in an existing mach command module. E.g:

```python
from mach.decorators import SettingsProvider
from mozbuild.base import MachCommandBase


@SettingsProvider
class ArbitraryClassName(MachCommandBase):
    config_settings = [
        ("foo.bar", "string", "A helpful description"),
        ("foo.baz", "int", "Another description", 0, {"choices": set([0, 1, 2])}),
    ]
```

`@SettingsProvider`'s must specify a variable called `config_settings`
that returns a list of tuples. Alternatively, it can specify a function
called `config_settings` that returns a list of tuples.

Each tuple is of the form:

```python
("<section>.<option>", "<type>", "<description>", default, extra)
```

`type` is a string and can be one of:
string, boolean, int, pos_int, path

`description` is a string explaining how to define the settings and
where they get used. Descriptions should ideally be multi-line paragraphs
where the first line acts as a short description.

`default` is optional, and provides a default value in case none was
specified by any of the configuration files.

`extra` is also optional and is a dict containing additional key/value
pairs to add to the setting's metadata. The following keys may be specified
in the `extra` dict:

- `choices` - A set of allowed values for the setting. May also be a
  callable returning an iterable of allowed values, useful when the set
  of valid values isn't known until runtime.

### Wildcards

Sometimes a section should allow arbitrarily defined options from the user, such
as the `alias` section mentioned above. To define a section like this, use `*`
as the option name. For example:

```{eval-rst}
.. parsed-literal::

    ('foo.*', 'string', 'desc')
```

This allows configuration files like this:

```{eval-rst}
.. parsed-literal::

    [foo]
    arbitrary1 = some string
    arbitrary2 = some other string

```

## Finding Settings

You can see which settings are available as well as their description and
expected values by running:

```{eval-rst}
.. parsed-literal::

    ./mach settings  # or
    ./mach settings --list

```

## Accessing Settings

Now that the settings are defined and documented, they're accessible from
individual mach commands from the mach command context.
For example:

```python
from mach.decorators import (
    Command,
    SettingsProvider,
)
from mozbuild.base import MachCommandBase


@SettingsProvider
class ExampleSettings(object):
    config_settings = [
        ("a.b", "string", "desc", "default"),
        ("foo.bar", "string", "desc"),
        ("foo.baz", "int", "desc", 0, {"choices": set([0, 1, 2])}),
    ]


@Command("command", category="misc", description="Prints a setting")
def command(command_context):
    settings = command_context._mach_context.settings
    print(settings.a.b)
    for option in settings.foo:
        print(settings.foo[option])
```
