# Turning on Firefox tests for a new configuration

You are ready to go with turning on Firefox tests for a new config. Once you
get to this stage, you will have seen a try push with all the tests running
(many not green) to verify some tests pass and there are enough machines
available to run tests.

For the purpose of this document, assume you are tasked with upgrading Windows
10 OS from version 1803 -> 1903. To simplify this we can call this `windows_1903`,
and we need to:

- create meta bug
- push to try
- run skip-fails
- repeat 2 more times
- land changes and turn on tests
- turn on run only failures

If you are running this manually or on configs/tests that are not supported with
`./mach try --new-test-config`, then please follow the steps [here](manual.md)

## Create Meta Bug

This is a simple step where you create a meta bug to track the failures associated
with the tests you are greening up. If this is a test suite (i.e. `devtools`), it
is ok to have a meta bug just for the test suite and the new platform.

All bugs related to tests skipped or failing will be blocking this meta bug.

## Push to Try Server

Now that you have a configuration setup and machines available via try server, it
is time to run try. If you are migrating mochitest or xpcshell, then you can do:

`./mach try fuzzy --no-artifact --full --rebuild 10 --new-test-config -q 'test-windows10-64-1903 mochitest-browser-chrome !ccov !ship !browsertime !talos !asan'`

This will run many tests (thanks to --full and --rebuild 10), but will give plenty
of useful data.

In the scenario you are migrating tests such as:

: - performance

- web-platform-tests
- reftest / crashtest / jsreftest
- mochitest-webgl (has a different process for test skipping)
- cppunittest / gtest / junit
- marionette / firefox-ui / telemetry

  then please follow the steps [here](manual.md)

  If you are migrating to a small machine pool, it is best to avoid `--rebuild 10` and
  instead do `--rebuild 3`. Likewise please limit your jobs to be the specific test
  suite and variant. The size of a worker pool is shown at the Workers page of the
  Taskcluster instance.

## Run skip-fails

When the try push is completed it is time to run skip-fails. Skip-fails will
look at all the test results and automatically create a set of local changes
with skip-if conditions to green up the tests faster.

`./mach manifest skip-fails --b bugzilla.mozilla.org -m <meta_bug_id> --turbo "https://treeherder.mozilla.org/jobs?repo=try&revision=<rev>"`

Please input the proper `meta_bug_id` and `rev` into the above command.

The first time running this, you will need to get a [bugzilla api key](https://bugzilla.mozilla.org/userprefs.cgi?tab=apikey). copy
this key and add it to your `~/.config/python-bugzilla/bugzillarc` file:

```none
cat bugzillarc
[DEFAULT]
url = https://bugzilla.mozilla.org
[bugzilla.mozilla.org]
api_key = <key>
```

When the command finishes, you will have new bugs created that are blocking the
meta bug. In addition you will have many changes to manifests adding skip-if
conditions. For tests than fail 40% of the time or for entire manifests that
take >20 minutes to run on opt or >40 minutes on debug.

You will need to create a commit (or `--amend` your previous commit if this is round 2 or 3):

`git commit -m "Bug <meta_bug_id> - Green up tests for <suite> on <platform>"`

## Repeat 2 More Times

In 3 rounds this should be complete and ready to submit for review and turn on
the new tests.

There will be additional failures, those will follow the normal process of
intermittents.

## Land Changes and Turn on Tests

After you have a green test run, it is time to land the patches. There could
be changes needed to the taskgraph in order to add the new hardware type and
duplicate tests to run on both the old and the new, or create a new variant and
denote which tests to run on that variant.

Using our example of `windows_1903`, this would be a new worker type that
would require these edits:

- {searchfox}`transforms/tests.py <taskcluster/taskgraph/transforms/tests.py#97>` (duplicate windows 10 entries)
- {searchfox}`test-platforms.py <taskcluster/kinds/test/test-platforms.yml#229>` (copy windows10 debug/opt/shippable/asan entries and make win10_1903)
- {searchfox}`test-sets.py <taskcluster/kinds/test/test-sets.yml#293>` (ideally you need nothing, otherwise copy `windows-tests` and edit the test list)

In general this should allow you to have tests scheduled with no custom flags
in try server and all of these will be scheduled by default on
`mozilla-central`, `autoland`, and `release-branches`.

## Turn on Run Only Failures

Now that we have tests running regularly, the next step is to take all the
disabled tests and run them in the special failures job.

We have a basic framework created, but for every test harness (i.e. xpcshell,
mochitest-gpu, browser-chrome, devtools, web-platform-tests, crashtest, etc.),
there will need to be a corresponding tier-3 job that is created.

TODO: point to examples of how to add this after we get our first jobs running.
