=================================
Performance Testing in a Nutshell
=================================

.. contents::
    :depth: 3

.. note::
  This page is still under construction, and is missing the videos.

This document provides all the information needed regarding the basics of performance testing our products. A set of videos, and instructions can be found to guide you across the various subjects related to performance testing. If you're new to performance testing, starting from the first section would be the most helpful for you.

Help! I have a regression. What do I do?
----------------------------------------

Most people will first be introduced to performance testing at Mozilla through a bug like this:

 .. image:: ./perf_alert_bug.png
   :alt: Performance Alert Bug
   :scale: 75%
   :align: center


The title of the bug will include the tests, and subtests that caused the largest change, the platform(s) the changes were detected on, and the date. It will also be prefixed with a range of the size (in percentage) of the changes. In the bug details, the bug which triggered the alert will also be found in the ``Regressed by`` field, and there will be a needinfo for the patch author.

**Bug overview and the first comment (aka comment 0):**

An alert summary table will always be found in the first comment (comment 0) of a performance regression bug, it provides all the information required to understand what regressed.

 .. image:: ./perf_alert_comment_zero.png
   :alt: Comment Zero from a Performance Alert Bug
   :scale: 75%
   :align: center


 * In the first sentence of the alert, a link to the actual push/commit that caused it is provided.
 * Below that, a table that summarizes the regressions, and improvements is shown.
 * Each row in the table represents a detected regression or improvement.

    * ``Ratio``: Provides the size of the change, and links to a graph for the metric.
    * ``Test``: The name of the test, and metric that changed. It links to the documentation for the test.
    * ``Platform``: The name of the platform that the change was detected on.
    * ``Options``: Specify the conditions in which the test was run (e.g. cold, warm, bytecode-cached).
    * ``Absolute Values``: The absolute values of the change.
    * ``Performance Profiles``: Before, and after Gecko profiles of the test (this is not available for all test frameworks yet).

 * Below the table, an ``alert summary`` link can be found which provides a different view of this table, a full in-depth explanation of the alert summary link can be found here `Perfherder Alerts View`_. Some additional debugging information such as ``side-by-side`` videos can be found here. It will also include any additional tests that triggered alerts after this bug was filed.
 * Then some information on what we expect from the patch author is provided regarding how long they have to respond to the alert before it gets backed out along with links to the guidelines for handling regressions.
 * Finally, a helpful command to run these tests on try is provided using ``./mach try perf --alert <ALERT-NUM>``. :ref:`See here for more information about mach try perf <Mach Try Perf>`.

**From the alert summary comment, there are multiple things that could be verified:**
 * Check the graphs to ensure that the regression/improvements are very visible.
 * Look at the alert summary to see if there are any available side-by-side videos to visualize.
 * Check the test description to see what the test is doing, as well as what the metrics that changed are measuring.
 * Compare the before/after performance profiles to see what might have changed. See the section on `Using the Firefox Profiler`_ for more information on this.

With all of the information found from those, there are two main things that can be done. The first is investigating the profiles (or using some other tools), and finding an issue that needs to be fixed. During the process of investigating and verifying the fix, it will become necessary to verify the fix by running the test. Proceed to the `Running Performance Tests`_ section for information about this, and then the `Performance Comparisons`_ section for help with doing performance comparisons in CI which is needed for verifying if a fix will resolve the alert.

The second is requesting a confirmation from the performance sheriff that the patch which caused the alert is definitely the correct one. This can happen when the metric is very noisy, and the change is small (in the area of 2-3%, our threshold of detection). The sheriff will conduct more retriggers on the test, and may ask some clarifying questions about the patch.

If there are any questions about the alert, or additional help is needed with debugging the alert feel free to needinfo the performance sheriff that reported the bug. The performance sheriff most suitable for adding a needinfo to can be identified in comment zero of the bug.

Alert Resolution
----------------

**There are 4 main resolutions for these alert bugs which depend on what you find in your investigations:**
 #. A ``WONTFIX`` resolution which implies that a change was detected, but it won't be fixed. It's possible to have this resolution on a bug which produces regressions, but the improvements outweigh those regressions.
 #. A ``WORKSFORME`` resolution which implies that a detection was valid, but there was no actual change to the product's performance. Harness-related changes are often resolved this way as well since we consider them baseline changes and do not change the performance characteristics of the product.
 #. An ``INVALID`` resolution which implies that the detection was invalid, and there wasn't a change to performance metrics. These are generally rare, as performance sheriffs tend to invalidate the alerts before a bug is produced, and tend to be related to infrastructure changes or very noisy tests where a culprit can't be determined accurately.
 #. A ``FIXED`` resolution which implies that a change was detected, and a fix was made to resolve it.

If it's unclear when an alert may be resolved, it's recommended to file a follow-up bug, and close the alert as ``INCOMPLETE``. If this cannot be done, then it's strongly recommended to reach out to the performance sheriff.

Alert Monitoring, and the Regression Policy
-------------------------------------------

There is a bugbot rule that monitors the activity of performance alerts. After 1 week of inactivity in the bug, a needinfo will be made for the regressor author to provide an update on any progress. This also notifies performance sheriffs.

If an alert is flagged in one of these notifications, and performance sheriffs find that there has been absolutely no activity on the alert since it was filed, then **it will become a candidate for a backout** in accordance with our `regression policy <https://www.mozilla.org/en-US/about/governance/policies/regressions/>`_. Otherwise, if there has been some activity, we will simply request an update on any progress that has been made.

The full process for performance sheriffs who handle these notifications proceeds as follows (`more detailed information can be found here <perf-sheriffing.html#how-to-handle-inactive-alerts>`_):
 #. A daily email of alert bugs with no activity is obtained.
 #. We check if the developer (regressor author) has previously responded to the bug.
 #. **If they have responded in the past**, we reach out to ask them to provide an update on the progress that has been made.
 #. **If they have not responded in the past**, we reach out to ask them to provide an update and mention that the regressor patch has been **added as a candidate for backout** due to lack of activity.
 #. If the developer is not responding in either of these cases, after 24 hours, we reach out to their manager with similar messages.
 #. If the patch is a candidate for backout, and neither the regressor author or their manager responds after 24 hours, we will request a backout of the regressor patch.

Closing the bug with an `Alert Resolution`_ would be the ideal way to get the Bugbot to stop setting needinfos. Otherwise, it expects some activity there on a weekly basis if it's being actively investigated. If the alert should be investigated some day, it could be closed as ``INCOMPLETE`` with a follow-up bug. There's also a keyword you can add if you absolutely want to keep it open though. The keyword is ``backlog-deferred`` but please only use it sparingly otherwise these kinds of bugs often end up sitting around for years and then get closed when they can no longer be reproduced in our CI (due to machine, test, platform changes).

Perfherder Alerts View
----------------------
When you click on the "Alerts Summary" hyperlink it will take you to an alert summary table on Perfherder which looks like the following screenshot:

 .. image:: ./perfherder_alertsview.png
   :alt: Sample Perfherder Alert Summary
   :scale: 75%
   :align: center

 * The table has 1 performance metric per row that has either improved or regressed a metric.
 * From left to right, the columns and icons you need to be concerned about as a developer are:

    * ``Graph icon``: Takes you to a graph that shows the history of the metric.
    * ``Test``: A hyperlink to all the test settings, test owner, and their contact information. As well as the name of the subtest (in our case SpeedIndex, and loadtime).
    * ``Platform``: Platform of metric which regressed.
    * ``Debug Tools``: Tools available to help visualize and debug regressions.
    * ``Information``: Historical data distribution (multimodal data, ok, or n/a if not enough information is  available).
    * ``Tags & Options``: Specify the conditions in which the test was run (e.g. cold, warm, bytecode-cached).
    * ``Magnitude of Change``: How much the metric improved or regressed (green colour indicates an improvement and red indicates a regression).
    * ``Confidence``: Confidence value of metric (number is not out of 100) higher number means higher confidence.

Running Performance Tests
-------------------------

Performance tests can either be run locally, or in CI using try runs. In general, it's recommended to use try runs to verify the performance changes your patch produces (if any). This is because the hardware that we run tests on may not have the same characteristics as local machines so local testing may not always produce the same performance differences. Using try runs also allows you to use our performance comparison tooling such as `Compare View <https://treeherder.mozilla.org/perfherder/comparechooser>`_ and `PerfCompare <https://perf.compare/>`_. See the `Performance Comparisons`_ section for more information on that.

It's still possible that a local test can reproduce a change found in CI though, but it's not guaranteed. To run a test locally, refer to the harness documentation test lists, such as this one for `Raptor tests <raptor.html#raptor-tests>`_. There are four main ways that you'll find to run these tests:

 * ``./mach raptor`` for :ref:`Raptor`
 * ``./mach talos-test`` for :ref:`Talos`
 * ``./mach perftest`` for :ref:`MozPerftest`
 * ``./mach awsy`` for :ref:`AWSY`

It's also possible to run all the alerting tests using ``./mach perftest``. To do this, find the alert summary ID/number, then use it in the following command::

   ./mach perftest <ALERT-NUMBER>

To run the exact same commands as what is run in CI, add the ``--alert-exact`` option. The test(s) to run can also be specified by using the ``--alert-tests`` option.

Performance Comparisons
-----------------------

Comparing performance metrics across multiple try runs is an important step in the performance testing process. It's used to ensure that changes don't regress our metrics, to determine if a performance improvement is produced from a patch, and among other things, used to verify that a fix resolves a performance alert.

We currently use PerfCompare for comparing performance numbers. Landing on PerfCompare, two search comparison workflows are available: Compare with a base or Compare over time. Compare with a base allows up to three new revisions to compare against a base revision. Although talos is set at the default, any other testing framework or harness can also be selected before clicking the Compare button. :ref:`You can find more information about using PerfCompare here <PerfCompare>`.

 .. image:: ./perfcomparehomescreen.png
   :alt: PerfCompare Selection Interface for Revisions/Pushes to Compare
   :scale: 50%
   :align: center

Our old tool for comparing performance numbers, `Compare View <https://treeherder.mozilla.org/perfherder/comparechooser>`_, will be replaced by PerfCompare early next year. The first interface that's seen in that process is the following which is used to select two pushes (based on the revisions) to compare.

 .. image:: ./compare_view_selection.png
   :alt: Selection Interface for Revisions/Pushes to Compare
   :scale: 50%
   :align: center

At the same time, the framework to compare will need to be selected. By default, the Talos framework is selected, but this can be changed after the Compare button is pressed.

After the compare button is pressed, a visualization of the comparisons is shown. More information on what the various columns in the comparison mean can be found in `this documentation <standard-workflow.html#compareview>`_.


Using the Firefox Profiler
--------------------------

The Firefox Profiler can be used to help with debugging performance issues in your code. `See here for documentation <https://profiler.firefox.com/docs/#/>`_ on how it can be used to better understand where the regressing code is, and what might be causing the regression. Profiles are provided on most alert summary bugs from before, and after the regression (see first section above).

If those are not provided in the alert summary, they can always be generated for a test by clicking on the graphs link (the percent-change ratio in an alert summary), selecting a dot in the graph from before or after a change, and clicking the job link. Then, once the job panel opens up in Treeherder, select ``Generate performance profile`` to start a new task that produces a performance profile. See the following graphic illustrating this process:

 .. image:: ./perf_alert_profile_from_graph.png
   :alt: Getting a Profile from an Alerting Test
   :scale: 75%
   :align: center

Additionally, you can also use the overflow menu and generate a profile:

 .. image:: ./create_profile_triple_dot.png
   :alt: Creating a profile through the overflow menu
   :scale: 50%
   :align: center

Most Raptor/Browsertime tests produce a performance profile by default at the end of their test run, but Talos, MozPerftest, and AWSY tests do not. As previously mentioned, for regression/improvement alerts, you can find a before and after link of these profiles in Comment 0:

 .. image:: ./perf_alert_comment_zero_before-after.png
   :alt: View before/after profiles from alerts
   :scale: 50%
   :align: center

You can also find the profiles in the artifacts tab of the Raptor test:

 .. image:: ./raptor_extra_profiler_run.png
   :alt: Find extra profiler run profiles in treeherder task
   :scale: 50%
   :align: center

To generate the profiles locally, you can pass the flags ``--extra-profiler-run`` or ``--gecko-profile`` which repeat the test for an extra iteration with the profiler enabled, or run the test from the beginning with the profiler enabled for three iterations, respectively. It's also possible to specify more configuration such as the profiled threads, the sampling interval or the profiler features being enabled. The parameters used in a profiling run can be copied directly from the about:profiling page in any Nightly build: click the button at the top of the page, then pick the option "Copy parameters for performance tests".


Triggering Gecko Profiles in CI with Custom Parameters
------------------------------------------------------

In addition to using creating a Gecko Profile with the treeherder actions, you can also generate a Gecko Profiler run with custom options **directly in CI** using the ``geckoprofile`` Treeherder action after navigating to ``Custom Action``.

This is useful when you want to profile an existing task using custom profiler settings such as specific threads, sampling intervals, or features, without needing to submit a new try push with non-default parameters.

How to trigger a custom Gecko Profile
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

1. Go to the push in Treeherder that ran the test you're interested in.
2. Click on the task (e.g. a Raptor or Talos test).

 .. image:: ./gpc_select.png
    :alt: Selected performance test
    :scale: 50%
    :align: center

3. In the task detail panel, click the three-dot menu and select ``Custom Action``.

 .. image:: ./gpc_custom.png
    :alt: Selecting custom action from the overflow menu
    :scale: 50%
    :align: center

4. From the list of actions, choose ``geckoprofile``.

 .. image:: ./gpc_dropdown.png
    :alt: Selecting geckoprofile-custom action
    :scale: 50%
    :align: center

5. Fill in the profiling parameters:

   * ``gecko_profile_interval`` – sampling interval in milliseconds (e.g. `1`)
   * ``gecko_profile_features`` – comma-separated feature list (e.g. `js,stackwalk,cpu,screenshots,memory`)
   * ``gecko_profile_threads`` – comma-separated thread names (e.g. `GeckoMain,Compositor,Renderer`)

 .. image:: ./gpc_form.png
    :alt: Interface to specify custom gecko profiling parameters
    :scale: 50%
    :align: center

 For an overview of available features and thread options, visit ``about:profiling`` in Firefox.
 It shows descriptions and internal names for features, and includes basic thread options.
 Note that the full set of threads is not documented. Teams typically know the ones they need, and may use presets.


Triggering Simpleperf Profiles in CI
------------------------------------

`Simpleperf is an android profiling tool  <https://firefox-source-docs.mozilla.org/performance/profiling_with_simpleperf.html#profiling-on-android-with-simpleperf>`_  which can profile all threads on an android device and also works for non-fenix apps.
At the moment, we only support generating Simpleperf profiles for video applink startup tests via the Treeherder UI.

To trigger a CI run, go to one of those jobs, and click the `Generate Performance Profile` button.

 .. image:: ./generate_performance_profile.png
    :alt: Generate Performance Profile Button
    :scale: 50%
    :align: center

When the job completes, click on the artifacts tab, and then click the `Open in Firefox Profiler` hyperlink. You'll be re-directed to a screen where you can then load and interact with the simpleperf profiles.

Native Profiling
----------------
To generate CI profiles with an OS-specific profiler (i.e., Simpleperf for Android), use the ``--native-profiling`` flag with ``./mach try`` and select a `supported job <browsertime.html#native-profiling-with-raptor-browsertime>`_.

Side-by-Side
------------

Side-by-Side is a job that compares the visual metrics provided by Browsertime for two consecutive pushes. This job is only applicable to pageload-type jobs, and both revisions must run on the same platform with identical configuration.

Generate a Side-by-Side Job
^^^^^^^^^^^^^^^^^^^^^^^^^^^

To generate a side-by-side job, follow these steps:

 * Select the desired job.
 * Navigate to the "Performance" tab and press the "Generate side-by-side" button.

 .. image:: ./generate-side-by-side.png
   :alt: Generate side-by-side task
   :scale: 50%
   :align: center

Once the process is initiated, a new job will appear in the format `side-by-side-*job-name*`.

Viewing Results
^^^^^^^^^^^^^^^
Once the newly generated job is complete, you can view the result by selecting the job. This type of job provides two types of video (`cold and warm  <raptor-metrics.html#warm-pageload>`_). For each set, there are 2 viewing modes (normal and slow-motion).

Each video includes an annotated description containing the visual metrics provided by Browsertime.

Adding Performance Tests
------------------------

This section is under construction.


Additional Help
---------------

Reach out to the Performance Testing, and Tooling team in the `#perftest channel on Matrix <https://matrix.to/#/#perftest:mozilla.org>`_, or the #perf-help channel on Slack.
