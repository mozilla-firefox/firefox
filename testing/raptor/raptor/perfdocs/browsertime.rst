##################
Raptor Browsertime
##################

.. contents::
   :depth: 2
   :local:

Browsertime is a harness for running performance tests, similar to Mozilla's Raptor testing framework. Browsertime is written in Node.js and uses Selenium WebDriver to drive multiple browsers including Safari, Safari Technology Preview, Chrome, Chrome for Android, Firefox, and Firefox for Android and GeckoView-based browsers.

Source code:

- Our current Browsertime version uses the canonical repo.
- In-tree: https://searchfox.org/mozilla-central/source/tools/browsertime and https://searchfox.org/mozilla-central/source/taskcluster/scripts/misc/browsertime.sh

Running Locally
---------------

**Prerequisites**

- A local Mozilla Repository clone with a `successful Firefox build </setup>`_ completed

Running on Firefox Desktop
--------------------------

Vanilla Browsertime tests
-------------------------

If you want to run highly customized tests, you can make use of our customizable ``browsertime`` test.

With this test, you can customize the page to test, test script to use, and anything else required. It will make use of default settings that Raptor uses in browsertime but these can be overridden with ``--browsertime-arg`` settings.

For example, here's a test on ``https://www.sitespeed.io`` using this custom test:

::

  ./mach raptor -t browsertime --browsertime-arg test_script=pageload --browsertime-arg browsertime.url=https://www.sitespeed.io --browsertime-arg iterations=3

That test will perform 3 iterations of the given url. Note also that we can use simplified names to make use of test scripts that are built into raptor. You can use ``pageload``, ``interactive``, or provide a path to another test script.

This custom test is only available locally.

Page-load tests
---------------
There are two ways to run performance tests through browsertime listed below.

**Note that** ``./mach browsertime`` **should not be used when debugging performance issues with profiles as it does not do symbolication.**

* Raptor-Browsertime (recommended):

::

  ./mach raptor -t google-search

* Browsertime-"native":

::

    ./mach browsertime https://www.sitespeed.io --firefox.binaryPath '/Users/{userdir}/moz_src/mozilla-unified/obj-x86_64-apple-darwin18.7.0/dist/Nightly.app/Contents/MacOS/firefox'

Benchmark tests
---------------
* Raptor-wrapped:

::

  ./mach raptor -t speedometer

Running on Android
------------------
To run on Android, the device needs to have Geckoview or Fenix installed on it. Our tests will only work with physical devices, and `bug 1881570 <https://bugzilla.mozilla.org/show_bug.cgi?id=1881570>`__ tracks progress for enabling virtual devices (emulators). Running either of the commands below will attempt to install locally built APKs to the device **while uninstalling/removing any existing APKs of the package on the device**, but this can be skipped by setting ``MOZ_DISABLE_ADB_INSTALL=1`` in your environment. When that environment variable exists, we expect the APK to be pre-installed on the device.

Running on Raptor-Browsertime (recommended):

* Running on Fenix

::

  ./mach raptor -t amazon --app fenix --binary org.mozilla.fenix

* Running on Geckoview

::

  ./mach raptor -t amazon --app geckoview --binary org.mozilla.geckoview_example

Running on vanilla Browsertime:

* Running on Fenix/Firefox Preview

::

    ./mach browsertime --android --browser firefox --firefox.android.package org.mozilla.fenix.debug --firefox.android.activity org.mozilla.fenix.IntentReceiverActivity https://www.sitespeed.io

* Running on the GeckoView Example app

::

  ./mach browsertime --android --browser firefox https://www.sitespeed.io

Running Power Usage tests on Mobile
-----------------------------------
To gather power usage tests on mobile, you can pass ``--power-test`` to ``./mach raptor``. This will only work if the phone is hooked up to a `supported USB power meter <https://github.com/fqueze/usb-power-profiling/tree/5a6fc823ea4dd0553f810f29a04c43a3c9c49147?tab=readme-ov-file#supported-devices>`_.

The data output is originally in picoWattHours, but are converted to microWattHours for the Perfherder Data JSON. The power usage is from the start of the pageload until the pageComplete check in browsertime completes.

Running on Google Chrome
------------------------
Chrome releases are tied to a specific version of ChromeDriver -- you will need to ensure the two are aligned.

There are two ways of doing this:

* Download the ChromeDriver that matches the chrome you wish to run from https://chromedriver.chromium.org/ and specify the path:

::

  ./mach browsertime https://www.sitespeed.io -b chrome --chrome.chromedriverPath <PATH/TO/VERSIONED/CHROMEDRIVER>

* Upgrade the ChromeDriver version in ``tools/browsertime/package-lock.json`` (see https://www.npmjs.com/package/@sitespeed.io/chromedriver for versions).

Run ``npm install``.

Launch vanilla Browsertime as follows:

::

  ./mach browsertime https://www.sitespeed.io -b chrome

Or for Raptor-Browsertime (use ``chrome`` for desktop, and ``chrome-m`` for mobile):

::

  ./mach raptor -t amazon --app chrome --browsertime-chromedriver <PATH/TO/CHROMEDRIVER>

Running on Safari Technology Preview
------------------------------------

Safari Technology Preview comes bundled with an appropriate ``safaridriver`` binary, and browsertime (via selenium) automatically launches this.

You will need to have installed the most up-to-date application version either by updating an existing installation on your MacOS or downloading it from https://developer.apple.com/safari/resources/ and ensure you download the one appropriate to your MacOS version

You can launch vanilla Browsertime with Safari Technology Preview as follows:

::

  ./mach browsertime https://www.sitespeed.io -b safari --safari.useTechnologyPreview


Or with Raptor-Browsertime

::

  ./mach raptor -t speedometer3 --app safari-tp --binary "/Applications/Safari Technology Preview.app/Contents/MacOS/Safari Technology Preview"

Running Page-load tests with third party WebExtensions
------------------------------------------------------
Page-load tests can also be executed on both Firefox Desktop and Firefox for Android builds with a set of popular
third party extensions installed (similarly to talos-realworld-webextensions, which runs tp5 tests with a set of third
party extensions installed).

Any of the page-load tests can be executed locally with the pre-selected set of third party extensions installed by just
adding to the base raptor command the additional ``--conditioned-profile settled-webext`` command line option.

Launch amazon tp6 page-load test on Firefox Desktop:

::

   ./mach raptor -t amazon --conditioned-profile settled-webext

Launch amazon tp6 mobile page-load test on Firefox for Android (the apk has to be pre-installed, mach raptor does detect if already installed but
it does not install it):

::

   ./mach raptor -t amazon --app fenix --binary org.mozilla.fenix --conditioned-profile settled-webext

To run these jobs on try, make sure to select the tp6 jobs that include the string `webextensions`, as an example (add ``--no-push`` to force try perf to only
list the jobs selected by the try perf query) to run all tp6 page-load webextensons jobs currently defined:

::

   ./mach try perf --full -q "'tp6 'webextensions"

Similarly for running tp6m (equivalent to tp6 but for mobile) on Firefox for Android builds:

::

   ./mach try perf --full -q "'tp6m 'webextensions"

The set of extensions installed are the ones listed in the ``"addons"`` property of the condprof customization file
`webext.json`_ from the ``testing/condprofile/condprof/customization/`` directory.

All extensions listed in the ``webext.json`` file are expected to have been predownloaded and included in the ``firefox-addons.tar`` archive
defined in the CI fetch config named `firefox-addons`_, but they will be automatically downloaded from the url specified in the ``webext.json``
file if they are not.

In a try push we allow to run jobs on new extension XPI files not part of the firefox-addons.tar archive, the new extension needs to be just
added in the `webext.json`_ condprof customization file in a patch part of the same stack of patches being pushed to try.

On the contrary new extensions added to the `webext.json`_ condprof customization file on mozilla-central patches will require the XPI file to be
added to the ``firefox-addons.tar`` archive and the `firefox-addons`_ CI fetch config updated accordingly (missing to update the archive will
trigger explicit linter errors, :doc:`see condprof-addons linter docs </code-quality/lint/linters/condprof-addons>`).

.. _webext.json: https://searchfox.org/mozilla-central/rev/bc6a50e6f08db0bb371ef7197c472555499e82c0/testing/condprofile/condprof/customization/webext.json
.. _firefox-addons: https://searchfox.org/mozilla-central/rev/bc6a50e6f08db0bb371ef7197c472555499e82c0/taskcluster/ci/fetch/browsertime.yml#169-176

TP6-Bench Test
--------------

The ``tp6-bench`` can either be run on desktop, or locally using the following command:

::

  ./mach raptor -t tp6-bench

This test runs through a selection of our tp6 pageload tests as quickly as possible while only measuring loadtime, FCP (First Contentful Paint), and LCP (Largest Contentful Paint). It does not measure visual metrics like our standard tp6 pageload tests do. The test produces 3 overall metrics (fcp, lcp, and loadtime) across all the pages tested that are summarized in two ways: geomean, and mean. The subtests for each of those metrics are the individual metrics produced in each of the pages tested.

To run this test on try and get a compare view link, run the following

::

  ./mach try perf -q "'Pageload 'Lite"



More Examples
-------------

`Browsertime docs <https://www.sitespeed.io/documentation/sitespeed.io/scripting/>`_

Passing Additional Arguments to Browsertime
-------------------------------------------

Browsertime has many command line flags to configure its usage, see `Browsertime configuration <https://www.sitespeed.io/documentation/browsertime/configuration/>`_.

There are multiple ways of adding additional arguments to Browsertime from Raptor. The primary method is to use ``--browsertime-arg``. For example: ``./mach raptor -t amazon --browsertime-arg iterations=10``

Other methods for adding additional arguments are:

* Define additional arguments in `testing/raptor/raptor/browsertime/base.py <https://searchfox.org/mozilla-central/source/testing/raptor/raptor/browsertime/base.py#220-252>`_.

* Add a ``browsertime_args`` entry to the appropriate manifest with the desired arguments, i.e. `browsertime-tp6.ini <https://searchfox.org/mozilla-central/source/testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.ini>`_ for desktop page load tests. `Example of browsertime_args format <https://searchfox.org/mozilla-central/source/testing/raptor/raptor/tests/custom/browsertime-process-switch.ini#27>`_.

Running Browsertime on Try
--------------------------

You can run all of our browsertime pageload tests through ``./mach try perf`` by selecting the ``Pageload`` category. We use chimera mode in these tests which means that both cold and warm pageload variants are running at the same time. There are a lot of other tests/categories available as well. Documentation about this tool can be found in :ref:`Mach Try Perf`.

For example, the following will select all ``Pageload`` categories to run on desktop:

::

  ./mach try perf -q "'Pageload 'desktop"

If you exclude the ``-q "..."`` option, an interface similar to the fuzzy interface will open, and show all available categories.

Visual Metrics
--------------

The following visual metrics are collected in all page load tests: ``ContentfulSpeedIndex, PerceptualSpeedIndex, SpeedIndex, FirstVisualChange, LastVisualChange``

Further information regarding these metrics can be viewed at `visual-metrics <https://www.sitespeed.io/documentation/sitespeed.io/metrics/#visual-metrics>`_

Gecko Profiling with Browsertime
--------------------------------

To run gecko profiling using Raptor-Browsertime you can add the ``--gecko-profile`` flag to any command and you will get profiles from the test (with the profiler page opening in the browser automatically). This method also performs symbolication for you. For example:

::

  ./mach raptor -t amazon --gecko-profile

It's also possible to specify more configuration such as the profiled threads, the sampling interval or the profiler features being enabled. The parameters used in a profiling run can be copied directly from the about:profiling page in any Nightly build: click the button at the top of the page, then pick the option "Copy parameters for performance tests".

Note that vanilla Browsertime does support Gecko Profiling but **it does not symbolicate the profiles** so it is **not recommended** to use for debugging performance regressions/improvements.

Gathering a Chrome trace with Raptor-Browsertime
------------------------------------------------

Browsertime supports the ability to profile Chrome applications and this functionality is now available in Raptor.

If running a Chrome/Chromium/Chromium-as-release test locally, simply add the ``--extra-profiler-run`` flag to your command line. By default the Chrome trace is run in CI now, and can be opened in the Firefox profiler UI.

Equivalent functionality to the ``--gecko-profile`` flag, i.e. something like ``--chrome-trace``, is not yet supported. That is currently tracked in `Bug 1848390 <https://bugzilla.mozilla.org/show_bug.cgi?id=1848390>`_

Custom profiling with Raptor-Browsertime
----------------------------------------

With browsertime you can now use the exposed start/stop commands of the gecko profiler **and** chrome trace. First, one needs to define the ``expose_browser_profiler`` and ``apps`` variables appropriately in the `test's configuration file <https://searchfox.org/mozilla-central/rev/11d085b63cf74b35737d9c036be80434883dd3f6/testing/raptor/raptor/tests/benchmarks/speedometer-desktop.ini#9,12>`_

If you want to run the test in CI then you will want to ensure you set the ``--extra-profiler-run`` flag in the mozharness extra options for where your test is defined in the `browsertime-desktop yaml file <https://searchfox.org/mozilla-central/rev/2e06f92ba068e32a9a7213ee726e8171f91605c7/taskcluster/ci/test/browsertime-desktop.yml#404-406>`_. Otherwise you can just pass the ``--extra-profiler-run`` flag locally in your command line.

Both of these steps are required to satisfy the ``_expose_browser_profiler()`` `method <https://searchfox.org/mozilla-central/rev/11d085b63cf74b35737d9c036be80434883dd3f6/testing/raptor/raptor/browsertime/base.py#241>`_ so that the option, `expose_profiler <https://searchfox.org/mozilla-central/rev/11d085b63cf74b35737d9c036be80434883dd3f6/testing/raptor/raptor/browsertime/base.py#383-386>`_, is passed into your browsertime script. Finally, it should be as simple as calling the ``start()`` & ``stop()`` commands in your `script <https://searchfox.org/mozilla-central/rev/11d085b63cf74b35737d9c036be80434883dd3f6/testing/raptor/browsertime/speedometer3.js#14,30-37,58-65>`_.

For the gecko profiler, you should also keep in mind these `default parameters <https://searchfox.org/mozilla-central/rev/2e06f92ba068e32a9a7213ee726e8171f91605c7/testing/raptor/raptor/browsertime/base.py#474-495>`_, which you may or may not want to change yourself in your tests configuration file.

Likewise, for chrome trace you will want to be aware of `these defaults. <https://searchfox.org/mozilla-central/rev/11d085b63cf74b35737d9c036be80434883dd3f6/testing/raptor/raptor/browsertime/base.py#646-658>`_

Upgrading Browsertime In-Tree
-----------------------------

To upgrade the browsertime version used in-tree you can run, then commit the changes made to ``package.json`` and ``package-lock.json``:

::

  ./mach browsertime --update-upstream-url <TARBALL-URL>

Here is a sample URL that we can update to: https://github.com/sitespeedio/browsertime/tarball/89771a1d6be54114db190427dbc281582cba3d47

To test the upgrade, run a raptor test locally (with and without visual-metrics ``--browsertime-visualmetrics`` if possible) and test it on try with at least one test on desktop and mobile.

Updating Benchmark Tests
------------------------

To upgrade any of our benchmark tests, you will need to change the revision used in the test manifest. There are three fields that you have available to use there: ``repository_revision`` to denote the revision, ``repository_branch`` to denote the branch name, and ``repository`` to provide the link of the Github repo that contains the benchmark.

For instance, with Speedometer 3 (sp3), we can update the revision `by changing the repository_revision field found here <https://searchfox.org/mozilla-central/rev/aa3ccd258b64abfd4c5ce56c1f512bc7f65b844c/testing/raptor/raptor/tests/benchmarks/speedometer-desktop.ini#29>`_. If the change isn't found on the default branch (master/main branch), then you will need to add an entry for ``repository_branch`` to specify this.

If the path to the test file changes (the file that is invoked to run the test), then the ``test_url`` will need to be changed.

Finding the Geckodriver Being Used
----------------------------------

If you're looking for the latest geckodriver being used there are two ways:
* Find the latest one from here: https://treeherder.mozilla.org/jobs?repo=mozilla-central&searchStr=geckodriver
* Alternatively, if you're trying to figure out which geckodriver a given CI task is using, you can click on the browsertime task in treeherder, and then click on the ``Task`` id in the bottom left of the pop-up interface. Then in the window that opens up, click on `See more` in the task details tab on the left, this will show you the dependent tasks with the latest toolchain-geckodriver being used. There's an Artifacts drop down on the right hand side for the toolchain-geckodriver task that you can find the latest geckodriver in.

If you're trying to test Browsertime with a new geckodriver, you can do either of the following:
* Request a new geckodriver build in your try run (i.e. through ``./mach try perf --show-all``).
* Trigger a new geckodriver in a try push, then trigger the browsertime tests which will then use the newly built version in the try push.

Comparing Before/After Browsertime Videos
-----------------------------------------

We have some scripts that can produce side-by-side comparison videos for you of the worst pairing of videos. You can find the script here: https://github.com/mozilla/mozperftest-tools#browsertime-side-by-side-video-comparisons

Once the side-by-side comparison is produced, the video on the left is the old/base video, and the video on the right is the new video.

Mach Browsertime Setup
----------------------

**WARNING**
 Raptor-Browsertime (i.e. ``./mach raptor -t <TEST>``) is currently required to be ran first in order to acquire the Node-16 binary. In general, it is also not recommended to use ``./mach browsertime`` for testing as it will be deprecated soon.

Note that if you are running Raptor-Browsertime then it will get installed automatically and also update itself. Otherwise, you can run:

- ``./mach browsertime --clobber --setup --install-vismet-reqs``

This will automatically check your setup and install the necessary dependencies if required. If successful, the output should read as something similar to:

::

    browsertime installed successfully!

    NOTE: Your local browsertime binary is at <...>/mozilla-unified/tools/browsertime/node_modules/.bin/browsertime

- To manually check your setup, you can also run ``./mach browsertime --check``

Known Issues
^^^^^^^^^^^^

With the replacement of ImageMagick, former cross platform installation issues have been resolved. The details of this can be viewed in the meta bug tracker
`Bug 1735410 <https://bugzilla.mozilla.org/show_bug.cgi?id=1735410>`_



- For other issues, try deleting the ``~/.mozbuild/browsertime`` folder and re-running the browsertime setup command or a Raptor-Browsertime test. Alternatively, you may need to delete the ``tools/browsertime/node_modules`` folder.

- If you plan on running Browsertime on Android, your Android device must already be set up (see more above in the :ref: `Running on Android` section)

- **If you encounter any issues not mentioned here, please** `file a bug <https://bugzilla.mozilla.org/enter_bug.cgi?product=Testing&component=Raptor>`_ **in the** ``Testing::Raptor`` **component.**
