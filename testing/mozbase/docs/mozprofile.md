# {mod}`mozprofile` --- Create and modify Mozilla application profiles

[Mozprofile] is a python tool for creating and managing profiles for Mozilla's
applications (Firefox, Thunderbird, etc.). In addition to creating profiles,
mozprofile can install [addons] and set preferences. Mozprofile can be utilized
from the command line or as an API.

The preferred way of setting up profile data (addons, permissions, preferences
etc) is by passing them to the [profile] constructor.

## Addons

```{eval-rst}
.. automodule:: mozprofile.addons
   :members:
```

Addons may be installed individually or from a manifest.

Example:

```
from mozprofile import FirefoxProfile

# create new profile to pass to mozmill/mozrunner
profile = FirefoxProfile(addons=["adblock.xpi"])
```

## Command Line Interface

```{eval-rst}
.. automodule:: mozprofile.cli
   :members:
```

The profile to be operated on may be specified with the `--profile`
switch. If a profile is not specified, one will be created in a
temporary directory which will be echoed to the terminal:

```
(mozmill)> mozprofile
/tmp/tmp4q1iEU.mozrunner
(mozmill)> ls /tmp/tmp4q1iEU.mozrunner
user.js
```

To run mozprofile from the command line enter:
`mozprofile --help` for a list of options.

## Permissions

```{eval-rst}
.. automodule:: mozprofile.permissions
   :members:
```

You can set permissions by creating a `ServerLocations` object that you pass
to the `Profile` constructor. Hosts can be added to it with
`add_host(host, port)`. `port` can be 0.

## Preferences

```{eval-rst}
.. automodule:: mozprofile.prefs
   :members:
```

Preferences can be set in several ways:

- using the API: You can make a dictionary with the preferences and pass it to
  the `Profile` constructor. You can also add more preferences with the
  `Profile.set_preferences` method.
- using a JSON blob file: `mozprofile --preferences myprefs.json`
- using a `.ini` file: `mozprofile --preferences myprefs.ini`
- via the command line: `mozprofile --pref key:value --pref key:value [...]`

When setting preferences from an `.ini` file or the `--pref` switch,
the value will be interpolated as an integer or a boolean
(`true`/`false`) if possible.

## Profile

```{eval-rst}
.. automodule:: mozprofile.profile
   :members:
```

## Resources

Other Mozilla programs offer additional and overlapping functionality
for profiles. There is also substantive documentation on profiles and
their management.

- profile [documentation]

[addonmanager]: https://searchfox.org/firefox-main/source/testing/mozbase/mozprofile/mozprofile/addons.py
[addons]: https://developer.mozilla.org/en/addons
[documentation]: http://support.mozilla.com/en-US/kb/Profiles
[here]: https://searchfox.org/firefox-main/source/testing/mozbase/mozprofile/mozprofile/permissions.py
[mozprofile]: https://searchfox.org/firefox-main/source/testing/mozbase/mozprofile
[mozprofile.profile]: https://searchfox.org/firefox-main/source/testing/mozbase/mozprofile/mozprofile/profile.py
