# Conditioned Profile

This project provides a command-line tool that is used to generate and maintain
a collection of Gecko profiles.

Unlike testing/profiles, the **conditioned profiles** are a collection of full
Gecko profiles that are dynamically updated every day.

Each profile is created or updated using a **scenario** and a
**customization**, and eventually uploaded as an artifact in TaskCluster.

The goal of the project is to build a collection of profiles that we can use in
our performance or functional tests instead of the empty profile that we
usually create on the fly with **mozprofile**.

Having a collection of realistic profiles we can use when running some tests
gives us the ability to check the impact of user profiles on page loads or
other tests.

A full cycle of how this tool is used in Taskcluster looks like this:

For each combination of scenario, customization and platform:

- grabs an existing profile in Taskcluster
- browses the web using the scenario, via the WebDriver client
- recreates a tarball with the updated profile
- uploads it as an index artifact into TaskCluster - maintains a changelog of each change

It's based on the Arsenic webdriver client <https://github.com/HDE/arsenic>

The project provides two **Mach** commands to interact with the conditioned
profile:

- **fetch-condprofile**: downloads a conditioned profile and deecompress it
- **run-condprofile**: runs on or all conditioned profiles scenarii locally

# How to download a conditioned profile

From the root of your Firefox checkout, run:

```
$ ./mach fetch-condprofile
```

This will grab the latest conditioned profile for your platform. But
you can also grab a specific profile built from any scenario or platform.

You can look at all the options with --help

# How to run a conditioned profile

If you want to play a scenario locally to modify it, run for example:

```
$ ./mach run-condprofile --scenario settled --visible /path/to/generated/profile
```

The project will run a webdriver session against Firefox and generate the profile.
You can look at all the options with --help

# Architecture

The conditioned profile project is organized into webdriver **scenarii** and
**customization** files.

## Scenarii

Scenarii are coroutines registered under a unique name in condprof/scenarii/\_\_init\_\_.py.

They get a **session** object and some **options**.

The scenario can do whatever it wants with the browser, through the webdriver session
instance.

See Arsenic's [API documentation](https://arsenic.readthedocs.io/en/latest/reference/session.html) for the session class.

Adding a new scenario is done by adding a module in condprof/scenarii/
and register it in condprof/scenarii/\_\_init\_\_.py

## Customization

A customization is a configuration file that can be used to set some
prefs in the browser and install some webextensions.

Customizations are JSON files registered into condprof/customizations,
and they provide four keys:

- **name**: the name of the customization
- **addons**: a mapping of add-ons to install.
- **prefs**: a mapping of prefs to set
- **scenario**: a mapping of options to pass to a specific scenario

In the example below, we install uBlock, set a pref, and pass the
**max_urls** option to the **heavy** scenario.

```json
{
   "name": "intermediate",
   "addons":{
      "uBlock":"https://addons.mozilla.org/firefox/downloads/file/3361355/ublock_origin-1.21.2-an+fx.xpi"
   },
   "prefs":{
      "accessibility.tabfocus": 9
   },
   "scenario": {
      "heavy": {"max_urls": 10}
   }
}
```
