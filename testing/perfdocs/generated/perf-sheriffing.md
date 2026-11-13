# Performance Sheriffing

```{contents}
:depth: 3
```

## 1 Overview

Performance sheriffs are responsible for making sure that performance changes in Firefox are detected
and dealt with. They look at data and performance metrics produced by the performance testing frameworks
and find regressions, determine the root cause, and file bugs to track all issues. The workflow we
follow is shown below in our flowchart.

Sheriffs review the alerts generated on [Perfherder](https://treeherder.mozilla.org/perfherder/alerts) on a daily
basis. Any time a test exceeds [the threshold set for its framework](#23-framework-thresholds), one or more alerts
are generated. The goal of the sheriff is to identify the commit or revision responsible for the change, and file a
bug with a needinfo for the author(s) of that commit or revision.

### 1.1 Flowchart

```{image} ./flowchart.png
:align: center
:alt: Sheriffing Workflow Flowchart
```

The editable source of the flowchart is available on [diagrams.net](https://drive.google.com/file/d/1Hpg9AjKTA2jx413Ly4imJWwM_gQHtLU3/view).

The workflow of a sheriff is backfilling jobs to get the data, investigating that data, filing
bugs/linking improvements based on the data, and following up with developers if needed.

### 1.2 Contacts and the Team

In the event that you have an urgent issue and need help what can you do?

If you have a question about a bug that was filed and assigned to you reach out to the sheriff who filed the bug on
Matrix. If a performance sheriff is not responsive or you have a question about a bug
send a message to the [Performance Sheriffs Matrix channel](https://chat.mozilla.org/#/room/#perfsheriffs:mozilla.org)
and tag the sheriff. If you still have no-one responding you can message any of the following people directly
on Slack or Matrix:

- [@afinder](https://people.mozilla.org/p/afinder)
- [@andra](https://people.mozilla.org/p/andraesanu)
- [@beatrice](https://people.mozilla.org/p/bacasandrei)
- [@florin.bilt](https://people.mozilla.org/p/fbilt)
- [@sparky](https://people.mozilla.org/p/sparky) (reach out to only if all others unreachable)

All of the team is in EET (Eastern European Time) except for @sparky who is in EST (Eastern Standard Time).

### 1.3 How to become a Sheriff

To become a sheriff (and have sheriffing permissions) you will need to file 2 bugs to allow sheriffing actions in Treeherder.

1) Request to be added to the Treeherder sheriff group

- Use this [pre-filled bug form](https://bugzilla.mozilla.org/enter_bug.cgi?assigned_to=nobody%40mozilla.org&bug_ignored=0&bug_severity=--&bug_status=NEW&bug_type=task&cf_a11y_review_project_flag=---&cf_accessibility_severity=---&cf_fx_iteration=---&cf_fx_points=---&comment=I%20will%20be%20sheriffing%20performance%20alerts%20on%20Perfherder%2C%20so%20I%20would%20like%20to%20be%20added%20to%20the%20Treeherder%20sheriff%20group.%0D%0A%0D%0AMy%20LDAP%20address%20is%3A%20&component=Treeherder%3A%20Infrastructure&contenttypemethod=list&contenttypeselection=text%2Fplain&defined_cc=cpeterson%40mozilla.com%2C%20cvalaas%40mozilla.com%2C%20dave.hunt%40gmail.com%2C%20jdescottes%40mozilla.com%2C%20sclements%40mozilla.com&defined_groups=1&filed_via=standard_form&flag_type-4=X&flag_type-41=X&flag_type-607=X&flag_type-803=X&flag_type-936=X&needinfo_role=other&needinfo_type=needinfo_from&op_sys=Unspecified&priority=--&product=Tree%20Management&rep_platform=Unspecified&short_desc=Add%20%3Cname%3E%20to%20Treeherder%20sheriff%20group&target_milestone=---&version=---). Add your name in the title and your LDAP email address in the description.

2) Add yourself to the perf_sheriff group

- Use this [pre-filled bug form](https://bugzilla.mozilla.org/enter_bug.cgi?assigned_to=infra%40infra-ops.bugs&bug_ignored=0&bug_severity=--&bug_status=NEW&bug_type=task&cf_fx_iteration=---&cf_fx_points=---&comment=I%20will%20be%20sheriffing%20performance%20alerts%20on%20Perfherder%2C%20so%20I%20would%20like%20to%20be%20added%20to%20the%20Treeherder%20sheriff%20group.%0D%0A%0D%0AMy%20LDAP%20address%20is%3A%20&component=Infrastructure%3A%20LDAP&contenttypemethod=list&contenttypeselection=text%2Fplain&defined_cc=cpeterson%40mozilla.com%2C%20&defined_groups=1&filed_via=standard_form&flag_type-4=X&flag_type-607=X&flag_type-674=X&flag_type-803=X&flag_type-936=X&groups=mozilla-employee-confidential&needinfo_role=other&needinfo_type=needinfo_from&op_sys=Unspecified&priority=--&product=Infrastructure%20%26%20Operations&rep_platform=Unspecified&short_desc=Add%20me%28%3CLDAP%20EMAIL%3E%29%20to%20perf_sheriff%20LDAP%20group&target_milestone=---&version=unspecified). Add your name and LDAP email in the title and your email in the description.

Once both bugs are completed check that you can do sheriffing actions. You should be able to now update alerts through the ability to add notes and tags to alerts.

## 2 How to Investigate Alerts

In this section we will go over how performance sheriffs investigate alerts.

### 2.1 Filtering and Reading Alerts

On the [Perfherder page](https://treeherder.mozilla.org/perfherder/alerts) you should see something like below:

```{image} ./Alerts_view.png
:align: center
:alt: Alerts View
```

After accessing the Perfherder alerts page make sure the filter (located in the top middle of the screenshot)
is set to show the correct alerts for sheriffing. The new alerts can be found when
the **untriaged** option from the left-most dropdown is selected. As shown in the screenshot below:

```{image} ./Alerts_view_toolbar.png
:align: center
:alt: Alerts View Toolbar
```

The rest of the dropdowns from left to right are as follows:

- **Testing harness**: altering this will take you to alerts generated on different harnesses
- **The filter input**, where you can type some text and press enter to narrow down the alerts view
- **"Hide downstream / reassigned to / invalid"**: enable this (recommended) to reduce clutter on the page
- **"My alerts"**: only shows alerts assigned to you.

Below is a screenshot of an alert:

```{image} ./single_alert.png
:align: center
:alt: Alert Summary
```

The alerts are grouped in **alert summaries**. You can tell a summary by looking at the bold text, it will say
"Alert #XXXXX", and each row inside of it is an alert for a test. By the book an alert is one item of a summary, but
the summary itself is often called an alert too, depending on the context. The tests in a summary:

- Can run on different platforms
- Can share suite name (like tp5o or tp6m)
- Measure various metrics, but not all of the metrics trigger alerts
- Share the same framework. If a commit triggers alerts in multiple frameworks, there will be a different summary for each framework

A summary can contain improvements, regressions or both. Improvements are marked in green and regressions in red.

Going from left to right of the columns inside the alerts starting with test, we have:

- A blue hyperlink that links to the test documentation (if available)
- The **platform's** operating system
- **Information** about the historical data distribution of that test
- Tags and options related to the test

### 2.2 Regressions vs Improvements

Whenever we get a performance change we classify it as one of two things, either a regression (worse performance) or
an improvement (better performance).

First thing to note about how we investigate alerts is that **we prioritize handling regressions**! Unlike the
**improvements,** regressions ship bugs to users, which, if not addressed, make our products worse and drive users away.
After acknowledging an alert:

- Regressions go through multiple status changes (see [Updating Alert Status](#212-updating-alert-status)) until they are finally resolved
- An improvement has a single status of improvement

### 2.3 Framework Thresholds

Different frameworks test different things, and the thresholds for triggering alerts and considering
performance changes differ based on the harness:

- AWSY >= 0.25%
- Build metrics installer size >= 100kb
- Talos, Browsertime, Build Metrics >= 2%

### 2.4 Taking an Alert and Golden Rules

Before doing any investigation, **assign the alert summary to yourself** by clicking the **Take** button in the summary and pressing Enter. You should see your username in its place afterwards.

When you are not sure about the culprit of the graph you are investigating, follow these golden rules:

- Zoom in until you can easily distinguish the data points
- Retrigger more if the graph is too unstable for the data you have
- When the graph is too noisy and zooming in only makes it harder: zoom out, choose a larger timeframe (30 days, 60 days, etc.), and then zoom back in slowly, following the limit line of the values in the first half of the graph until you find the data point that changes the trend

### 2.5 Reading the Graph

To read the graph of an alert, hover over the alert row and click the **graph** link that appears. Starring the alert makes it easy to know which alert you were reading when you come back to the summary.

```{image} ./Graph_view_alert_tooltip.png
:align: center
:alt: Graph view alert tooltip
```

The graph shows a thin vertical line for every alert associated with the test, so make sure you are looking at the right one by hovering over or clicking on the data point. If the data point of the improvement/regression is not clear you can:

- Zoom in by drawing a rectangle over the desired area
- Zoom out by clicking on the top graph
- Extend the timeframe of the graph using the dropdown at the top of the page

More details are available in the FAQ entries [How can I view details on a graph?](#how-can-i-view-details-on-a-graph) and [How can I zoom on a perfherder graph?](#how-can-i-zoom-on-a-perfherder-graph).

If the commit of the improvement/regression is not clear, take the desired action (usually retrigger/backfill) and write down in the notes of the alert (**Add/Edit notes**) your name and what you did, so that you or another sheriff knows what is happening the next time the alert is sheriffed. The pattern is `[yourname] comment`, and the most recent comments go first so they are easy to read when you come back.

```{image} ./Alert_summary_add_notes.png
:align: center
:alt: Alert summary add notes
```

### 2.6 Retrigger/Backfill

Depending on the test, the jobs run either once in several revisions or on every revision. The vast majority run once in several revisions (see [coalescing](#what-is-coalescing)), so almost every time you need to backfill between the first bad and the last good job to determine the culprit for sure.

1. Click the **job** link in the tooltip of the regression data point. A new page with the list of jobs for that data point opens.
2. Click the link next to **Job** in the lower left section of the page to narrow the list down to the job that caused the regression.

   ```{image} ./Treeherder_jobs_screen.png
   :align: center
   :alt: Treeherder jobs screen
   ```

3. Load the previous pushes to identify how many revisions you need to backfill, using the **get next** buttons (10, 20, 50) at the bottom of the page. Usually the lowest is enough.
4. With the top job selected, trigger the backfill action. Sheriffs usually use the quick **Backfill** button, but **Custom action...** lets you choose how many revisions to go back and how many retriggers per revision you want. Other actions are available, but they are not used by sheriffs.

After triggering the action, you must see a confirmation message at the top left of the screen within 5-7 seconds. If you don't see it, the jobs may not have been triggered and you risk losing investigation time.

Once the jobs finish running you have jobs on successive revisions and you can continue looking for the culprit.

### 2.7 Finding the Culprit

A clear improvement/regression usually appears when there is an easily noticeable difference between two adjacent data points:

```{image} ./Clear_improvement_graph.png
:align: center
:alt: Clear improvement graph
```

In other cases the difference is much less noticeable and the data of the test is more unstable. Some retriggers are needed to determine the range of the test data and compare it between several adjacent data points:

```{image} ./Unstable_improvement_graph.png
:align: center
:alt: Unstable improvement graph
```

Sometimes there is a gap of revisions between the data points in the graph, and a little investigation might save a manual bisection. [This video](https://mozilla.hosted.panopto.com/Panopto/Pages/Viewer.aspx?id=6f5cd956-d656-488c-bc91-b085009703c1) shows how to handle such a case.

The least fortunate situation is when the test is unstable, there are gaps in the graph where the tests didn't run, and the regression/improvement is almost impossible to determine. If the investigation takes more than 5 business days, ask for help if you haven't already:

- Ask the other sheriffs in the team
- See if there were similar situations in the past and how they were handled
- Find the framework owner on the [module ownership page](https://wiki.mozilla.org/Modules/All) and reach out to them on [Matrix](https://chat.mozilla.org), by email or any other convenient method
- If you still can't figure it out, ask your team lead

The investigation might end with a bug being opened without knowing the specific commit that caused the regression, asking the most relevant people for help:

```{image} ./Graph_uncertain_culprit.png
:align: center
:alt: Graph uncertain culprit
```

A less common case is when the graph is clear about the culprit but the patch contains changes unrelated to the platform(s) targeted by the alert. For example, the patch only modifies configuration for mobile platforms and the alert targets only desktop platforms. This **might** be an error somewhere, so we recommend asking in the culprit bug what might be missing before opening the regression bug or linking the improvement to it.

### 2.8 Identifying the Culprit Bug

Once the revision that caused the change is clear, you need to identify the culprit bug:

- If the revision contains changes from only one bug, open a regression bug for it
- If the revision contains changes from several bugs but you know the test and which of the bugs caused the regression, open a bug for that one
- If the revision contains changes from several bugs (usually a merge from one of the other repositories), do a [bisection](#how-can-i-do-a-bisection) to identify the bug

See also [Multiple Bug IDs on the Same Push](#2131-multiple-bug-ids-on-the-same-push).

### 2.9 Types of Alerts

An alert is not necessarily caused by a bug in the code. It can also be caused by the instability/noise of the test or by causes that are unrelated to the repository, like the CI setup.

A bug associated with an alert needs the `perf-alert` keyword. Regression bugs get it automatically when the **File bug** button is used, but bugs that are not regression bugs need it added manually.

#### 2.9.1 Harness Alerts

Harness alerts are usually caused by re-recordings or changes to code from the `testing/raptor` component that change the baseline. If the regression is expected, link the culprit directly to the alert, close it as WONTFIX and add the `harness` tag. Otherwise, open a regression bug.

##### Baseline Changes

Baseline changes are hard to identify. These questions help determine, with the rest of the team, whether we are dealing with a real regression or a baseline change:

1. **Is the patch changing the baseline metrics because we are aiming to test in a different environment?** If yes, the new baseline may be accepted. As the conditions for testing have changed we have to expect that some things will change. For instance, adding a blank profile to the conditioned profile changes means testing in a different environment, so we expect metrics to change.
2. **Is the patch changing something that shouldn't cause a shift in metrics?** If yes, do not accept the new baseline immediately. Do an analysis and discuss it with the other sheriffs to get second opinions.
3. **Does the patch change the test itself?** If yes, a new baseline may be accepted depending on the changes. If the changes are not functional, we shouldn't accept a new baseline.
4. **Are the test recordings changing?** If yes, a new baseline should be accepted after checking that the page being tested is different from the previous recording. If it isn't, the differences should be investigated further. It is possible that it is still a valid baseline change even if the page is the same, as non-visual changes may differ.
5. **Any other case** that doesn't fall into these categories should be discussed with the team for second opinions, and with the patch authors if needed.

#### 2.9.2 Backout/Regression-fix Alerts

These alerts are caused by backouts or fixes of regressions, and the associated tags are `regression-backedout` and `regression-fix` respectively. The status of the regressions can be changed to WONTFIX.

For alerts caused by a backout, reply to the backout comment with the summary of the alert and add the `perf-alert` keyword. The FAQ entry [What is a backout?](#what-is-a-backout) shows what a backout looks like on the graph.

#### 2.9.3 Infra Alerts

Regressions caused by infra changes are probably the most difficult to identify. Unless the infra change was announced and is known, an infra regression is usually detected by the sheriff after all the suspect commits/bugs were ruled out.

Anything that doesn't depend on the repository code is considered part of the CI infrastructure, so it doesn't depend on the code state at a certain point in history. For example, if the farm devices were updated with a changed OS image, no matter which data point from history is (re)triggered, it will run on the current image. So if the change of the OS image has an effect, the retriggers will reveal a difference between the old data points and the new ones of the same commit.

In the graph below, the values were constantly around 1800-2000 until Apr 17 and then dropped to 1200-1400. Backfilling didn't reveal the culprit, so jobs were retriggered further back. The retriggers fell in the interval of the improvement, and many yellow vertical lines mark infra changes on the graph.

```{image} ./Graph_infra_change.png
:align: center
:alt: Graph infra change
```

Infra alerts became more frequent starting in 2022, but the causes are still unknown. They usually come up in pairs of regression and improvement:

| Alert | Tags | Status |
|---|---|---|
| Regression | `infra` | **investigating** |
| Improvement that follows | `infra`, `regression-fix`, `improvement` | WONTFIX |
| Summary with regressions that follows | `infra`, `regression-fix` | WONTFIX |

In both follow-up cases, the status of the original regression alert is changed to **FIXED**.

#### 2.9.4 Invalid Alerts

Invalid regressions usually (but not only) happen when the test results are very unstable. A useful tip for finding invalid regressions is looking at the history of the graph for a pattern in the evolution of the data points.

In the graph below, the regression appeared around Dec 9, and the values vary predominantly between 0.7 and 1. If you click the first highlighted data point (around Dec 3), you will see that its alert is marked as invalid.

```{image} ./Graph_invalid_regression.png
:align: center
:alt: Graph invalid regression
```

Be careful: even if a graph has a wide varying interval, most of its data points may be concentrated around one value. In the graph below, the data points are concentrated around 1 and, after the alert around Dec 9, they stabilized around the regression's value (0.75-0.8). This is a real regression.

```{image} ./Graph_sccache_real_regression.png
:align: center
:alt: Graph sccache real regression
```

**sccache hit rate** tests are a particular case. Most of those alerts are invalid, but if the hit rate drops and stays low for at least 12-24 hours, a regression bug should be opened.

### 2.10 Handling Regressions

There are two different approaches to handling regressions:

- Filing a regression bug for actual regressions
- Letting the author of the culprit know that their patch caused a regression when we know that it will be accepted (backout, regression-fix, harness)

#### 2.10.1 Filing a Regression Bug

To file a bug from Perfherder you must be logged in.

1. Click the **Untriaged** status in the upper right side of the alert summary.
2. Select the **File bug** option and enter the number of the bug that caused the regression. You will be redirected to bugzilla.mozilla.org.
3. In Bugzilla, scroll down, click **Set bug flags** and set the last `status-firefox` version that appears in the list to **affected**.
4. Click the **Submit Bug** button at the bottom of the page.

#### 2.10.2 Filing a Regression Bug Manually

Make sure that there **isn't already a bug open** for the regression, see [How do I search for an already open regression?](#how-do-i-search-for-an-already-open-regression), then file one with these fields:

- **Type:** Defect
- **Keywords:** `perf`, `perf-alert`, `regression` (filled in automatically)
- **Blocks:** the meta-bug for the next Firefox release, used to keep track of the regressions of a specific release (see [How do I identify the current firefox release meta-bug?](#how-do-i-identify-the-current-firefox-release-meta-bug))
- **Regressed by:** the number of the bug that caused the regression (a closed bug number appears struck through)
- **Request information from:** the assignee of the culprit bug
- **CC:** at least the assignee, the reporter and the triage owner
- **Tracking flag:** click **Set bug flags** and set the last `status-firefox` version in the list to **affected**
- **Product and Component:** these are filled automatically in the **Enter bug** page. Save the bug, then edit them to be the same as in the culprit bug

#### 2.10.3 Linking and Acknowledging

After the regression bug is filed, link it to the summary (**Link to bug** in the summary menu) and change the status of the summary to **acknowledged**. Then follow the comments in the bug to make sure it gets closed, ideally before the next Firefox release.

#### 2.10.4 Questioned Regressions

The regression identified from the graph can be inaccurate for several reasons. When the author of the culprit patch/bug doesn't agree that their code caused the regression, take another look at the alert. A sheriff can, among other things:

- Retrigger/backfill the jobs around the regression, especially when the graph is noisy and the regression is not very clear. Sheriffs are used to reading the graphs and can see something clear that the patch author, who doesn't have that experience, can't.
- Check whether the alert contains 2 very close or neighboring regressions. If the alert contains tests with different names, they may be caused by different revisions and need to be confirmed or ruled out with retriggers/backfills.
- Check whether the patch only contains "static" code (comments, documentation updates). In that case another cause, like an infra change, is possible. Be careful, the developer doesn't have to know what the patch does, so sometimes this is only caught when the developer questions the regression.

Make sure to respond to any open questions or needinfos addressed to sheriffs in the regression bugs.

You can follow up on all the open regression bugs you created, see [How do I follow up on already open regressions open by me?](#how-do-i-follow-up-on-already-open-regressions-open-by-me). Regression bugs that become inactive are handled as described in [How to Handle Inactive Alerts](#3-how-to-handle-inactive-alerts).

### 2.11 Handling Improvements

Unlike for regressions, there is no need to open a bug for an improvement. Notify the bug assignee in a comment and add the `perf-alert` keyword to the bug.

#### 2.11.1 Valid Improvements

Use **Copy summary** in the summary menu, paste it as a "Congrats" comment in the bug that caused the improvement, add the `perf-alert` keyword to the bug and update the status of the summary to **acknowledged**.

**Attention:** the summary may contain alerts reassigned from other summaries. Tick the box next to **each untriaged alert** and change its status to **Acknowledge**. Ticking the box next to the alert summary and resetting it will **unlink** the reassigned alerts, which you don't want.

#### 2.11.2 Improvements Treated as Regressions

Depending on the test, an improvement of high magnitude (over 80%) should be treated more carefully. While a 100% improvement is impossible for a page load test (a site never loads instantly), over 80% is very rare and might mean that the test isn't loading what it should, for example an error page, which is likely to contain much less code than the actual website.

#### 2.11.3 Invalid Improvements

The same logic as for [invalid regressions](#294-invalid-alerts) applies. The difference is that the unstable graph triggered an alert while the value changed in the sense of an improvement.

### 2.12 Updating Alert Status

After finding the culprit and doing the necessary actions for the improvement/regression, update the tags and the status of the alert.

#### 2.12.1 Tags

Add a tag to the alert summary, using the tags option in the summary menu, if it fits one of the following:

| Tag | Used for |
|---|---|
| `harness` | Patches that updated the harness and caused improvements or regressions |
| `regression-backedout` | Patches backed out due to causing regressions |
| `regression-fix` | Patches fixing a reported regression bug |
| `infra` | Improvements or regressions caused by infra changes (not related to repository code) |
| `improvement` | Patches causing improvements |
| `improvement-backedout` | Regressions caused by backing out an earlier improvement |

`regression-backedout` and `regression-fix` apply to the alerts linked to the bugs that backed out or fixed a regression, **not** to the alerts linked to the regression bugs. `improvement` only applies to improvements, and `improvement-backedout` only to regressions.

#### 2.12.2 Moving Alerts out of the Untriaged Queue

After the revision that caused the alert was identified, move the alert out of the untriaged queue:

| Situation | New status |
|---|---|
| The alert is a valid improvement/regression and you linked a bug for it | **Acknowledge** |
| The alert is an invalid improvement/regression | **Invalid** |
| The alert is a downstream of an improvement/regression | **Downstream** |
| The improvement/regression happened earlier or later on the same repository | **Reassign** |

### 2.13 Special Cases

#### 2.13.1 Multiple Bug IDs on the Same Push

For **regressions**, if the push identified as the culprit has multiple revisions and bug IDs, fill in the "Regressed by" field of the filed bug with each of the bugs. If there is a regression-fix, regression-backedout or improvement-backedout bug to link to the alert but the revision contains multiple bugs, link the most relevant one. If you are unsure, needinfo the author to tell which of the bugs they think caused the alert.

For **improvements**, the general approach is to leave needinfos for the authors to ask for help identifying the corresponding bug.

#### 2.13.2 Release Flag Changes and Version Bumps on mozilla-beta

Alerts caused by modifying release flags (`EARLY_BETA_OR_EARLIER`) or bumping the Firefox version are generated on mozilla-beta and have no bugs associated with the revisions:

1. Check the milestone version in the changeset details.
2. Check if a bug was already created for this milestone version.
3. If not, clone [bug 1879080](https://bugzilla.mozilla.org/show_bug.cgi?id=1879080).
4. Leave a comment with the alert summary you were investigating.

Comment 0 can include the following information:

```
There were some alerts generated on mozilla-beta, that have no bugs associated with the revisions.
Alert: https://treeherder.mozilla.org/perf.html#/alerts?id=<alert#number>
Pushlog: https://hg.mozilla.org/releases/mozilla-beta/pushloghtml?fromchange=<rev>&tochange=<rev>
Changeset Details: https://hg.mozilla.org/releases/mozilla-beta/rev/<rev>

A few notes to consider:
* for mozilla-beta alerts, we normally search for downstreams on autoland
* most of the time, when a change is pushed to autoland, it is also pushed to mozilla-central during the same day
* on mozilla-beta the change is merged after ~ 6 weeks
```

#### 2.13.3 build_metrics

- **decision** alerts are usually just spikes and not relevant, so they are set to **invalid**. If the graph shows that the regression wasn't improved/fixed afterwards, check with the other sheriffs in the Performance Sheriffs Matrix channel, since it might be a cause for concern.
- **instrumented** in the Tags & Options column should be set to **invalid**. Alerting will be deactivated for these tests in the future.

#### 2.13.4 Regression Already Backed Out

If a regression alert summary is identified and the changeset was already backed out, the safe approach is to file a bug following the usual workflow, but mention in comment 0 that the changeset was already backed out and there is no action item for the author. The bug is only created for tracking purposes and to be able to acknowledge the summary of the alert.

#### 2.13.5 Jobs Failing to Run

If the jobs are failing to run, we usually resort to [bisection](#how-can-i-do-a-bisection).

#### 2.13.6 Invalid Comments

If you left a comment by mistake that turns out to be invalid, add the `obsolete` tag to the comment.

## 3 How to Handle Inactive Alerts

Inactive performance alerts are those alerts which have had no activity in 1 week. This section covers how performance sheriffs should handle inactive performance alerts that are found in the daily email sent to the [perfalert-activity group](https://groups.google.com/a/mozilla.com/g/perfalert-activity/about).

### 3.1 Process

The following is the general process that needs to be taken for the alerts in the email:

> 1. Open the email titled `[bugbot][autofix] PerfAlert regressions with 1 week(s) of inactivity for the DATE` to find bugs that are inactive.
>
>    - These occur at most daily.
>
> 2. Open one of the bugs mentioned in the email.
>
> 3. Check if the developer has previously responded to the bug.
>
> 4. Find the developer (regression author) being needinfo’ed by the BugBot.
>
> 5. (Optional) Check on [people.mozilla.org](https://people.mozilla.org) to find the person’s Matrix/Slack information if needed.
>
> 6. Find the developer in a public channel.
>
>    - `#developers` on Matrix is the most likely place you can find them.
>
> 7. Reach out to them with a message like the following:
>
>    - **If the patch has had a response from the regressor author:**
>
>      ```
>      Hello, could you provide an update on this performance regression or close it if it makes sense to (with a follow-up bug if needed)? [Bug ID](PERFORMANCE-ALERT-BUG-LINK)
>      ```
>
>    - **If the patch has never had a response from the regressor author:**
>
>      ```
>      Hello, could you provide an update on this performance regression or close it if it makes sense to (with a follow-up bug if needed)? In accordance with our [regression policy](https://www.mozilla.org/en-US/about/governance/policies/regressions/), we're considering backing out your patch due to a lack of comments/activity: [Bug ID](PERFORMANCE-ALERT-BUG-LINK)
>      ```
>
>    When reaching out, if there are multiple inactive bugs from the same regressor author, send a single message to that author that includes all of the relevant bugs, instead of sending a separate message (ping) for each bug.

### 3.2 Handling Responses

#### For Bugs with a Response from the Regressor Author

Depending on the developer's response, one of four things may happen:

> 1. **Developer provides an update on the alert bug:**
>
>    - No other action is needed. If this has happened multiple times on the bug, you can add the `backlog-deferred` keyword to prevent the BugBot rule from triggering again on the alert.
>
> 2. **Developer asks for clarification on the process or isn’t sure what to do:**
>
>    - Point them to this documentation. Explain the possible resolutions and what we expect of them.
>
> 3. **Developer does not respond:**
>
>    - Wait for 1 full business day for the response. If there is still no response, find and ping their manager (can be in private) from [people.mozilla.org](https://people.mozilla.org).
>
>      - If there is a response from the manager, you can proceed with one of the other options.
>
> 4. **Developer does not want to close the bug and needs time to investigate:**
>
>    - Add the `backlog-deferred` keyword to prevent BugBot from triggering on this bug again in the future.

#### For Bugs with No Previous Response from the Regressor Author

Depending on the developer's response, one of five things may happen:

> 1. **Developer agrees to a backout:**
>
>    - Reach out to a sheriff in `#sheriffs` on Matrix to request the backout.
>
>      - Ensure that they understand that if they’re actively working on it, they can provide an update on the alert bug to prevent a backout.
>      - Ensure that they understand that they can close the bug with `WONTFIX`/`INCOMPLETE` if they aren’t actively working on it, or they think it isn’t a big issue. They can file a follow-up bug to look into the issue further in the future. If it's been determined that there is no actual performance issue but there was a detection, they could close the bug as `WORKSFORME`.
>
> 2. **Developer provides an update on the alert bug:**
>
>    - No other action is needed. If this has happened multiple times on the bug, you can add the `backlog-deferred` keyword to prevent the BugBot rule from triggering again on the alert.
>
> 3. **Developer asks for clarification on the process or isn’t sure what to do:**
>
>    - Point them to this documentation. Explain the possible resolutions and what we expect of them.
>
> 4. **Developer does not respond:**
>
>    - Wait for 1 full business day for the response. If there is still no response, find and ping their manager (can be in private) from [people.mozilla.org](https://people.mozilla.org).
>
>      - If there is a response from the manager/developer, you can proceed with one of the other options. If not, request a backout.
>
> 5. **Developer does not want to close the bug and needs time to investigate:**
>
>    - Ask them to provide a comment in the bug stating this. Add the `backlog-deferred` keyword to prevent the BugBot from triggering on this bug again in the future.

## 4 FAQ

### What is Perfherder?

[Perfherder](https://treeherder.mozilla.org/perf.html#/graphs) is a tool that takes data points from log files and graphs them over time.
Primarily this is used for performance data from [Talos](https://wiki.mozilla.org/TestEngineering/Performance/Talos), but also from {doc}`AWSY </testing/perfdocs/awsy>`, build_metrics, [Autophone](https://wiki.mozilla.org/EngineeringProductivity/Autophone) and platform_microbenchmarks.
All these are test harnesses and you can find more about them [here](https://wiki.mozilla.org/TestEngineering/Performance/Sheriffing/Alerts).

The code for Perfherder can be found inside Treeherder [on GitHub](https://github.com/mozilla/treeherder/).

### How can I view details on a graph?

When viewing Perfherder Graph details, in many cases it is obvious where the regression is. If you mouse over the data points (not click on them) you can see some raw data values.

While looking for the specific changeset that caused the regression, you have to determine where the values changed. By moving the mouse over the values you can easily determine the high/low values historically to determine the normal 'range'. When you see values change, it should be obvious that the high/low values have a different 'range'.

If this is hard to see, it helps to zoom in to reduce the 'y' axis. Also zooming into the 'x' axis for a smaller range of revisions yields less data points, but an easier way to see the regression.

Once you find the regression point, you can click on the data point and it will lock the information as a popup. Then you can click on the revision to investigate the raw changes which were part of that.

```{image} ./Ph_Details.png
:align: center
:alt: Ph_Details
```

Note, here you can get the date, revision, and value. These are all useful data points to be aware of while viewing graphs.

Keep in mind, graph server doesn't show if there is missing data or a range of changesets.

### How can I zoom on a perfherder graph?

Perfherder graphs has the ability adjust the date range from a drop down box. We default to 14 days, but we can change it to last day/2/7/14/30/90/365 days from the UI drop down.

It is usually a good idea to zoom out to a 30 day view on integration branches. This allows us to see recent history as well as what the longer term trend is.

There are two parts in the Perfherder graph, the top box with the trendline and the bottom viewing area with the raw data points. If you select an area in the trendline box, it will zoom to that. This is useful for adjusting the Y-axis.

Here is an example of zooming in on an area:

```{image} ./Ph_Zooming.png
:align: center
:alt: Ph_Zooming
```

### How can I add more test series to a graph?

One feature of Perfherder graphs is the ability to add up to 7 sets of data points at once and compare them on the same graph. In fact when clicking on a graph for an alert, we do this automatically when we add multiple branches at once.

While looking at a graph, it is a good idea to look at that test/platform across multiple branches to see where the regression originally started at and to see if it is affected on different branches. There are 3 primary needs for adding data:

- investigating branches
- investigating platforms
- comparing pgo/non pgo/e10s for the same test

For investingating branches click the branch name in the UI and it will pop up the "Add more test data" dialog pre populated with the other branches which has data for this exact platform/test. All you have to do is hit add.

```{image} ./Ph_Addbranch.png
:align: center
:alt: Ph_Addbranch
```

For investigating platforms, click the platform name in the UI and it will pop up the "Add more test data" dialog pre populated with the other platforms which has data for this exact platform/test. All you have to do is hit add.

```{image} ./Ph_Addplatform.png
:align: center
:alt: Ph_Addplatform
```

To do this find the link on the left hand side where the data series are located at "+Add more test data":

```{image} ./Ph_Addmoredata.png
:align: center
:alt: Ph_Addmoredata
```

### How can a test series can be muted/hidden?

A test series from a perfherder graph can be muted/hidden by toggling on the checkbox on the lower right of the data series from the left side panel.

```{image} ./Ph_Muting.png
:align: center
:alt: SPh_Muting
```

### What makes branches different from one another?

We have a variety of branches at Mozilla, here are the main ones that we see alerts on:

- Mozilla-Inbound (PGO, Non-PGO)
- Autoland (PGO, Non-PGO)
- Mozilla-Beta (all PGO)

Linux and Windows builds have [PGO](#what-is-pgo), OSX does not.

When investigating alerts, always look for the Non-PGO branch first. Usually expect to find changes on Mozilla-Inbound (about 50%) and Autoland (50%).

The volume on the branches is something to be aware of, we have higher volume on Mozilla-Inbound and Autoland, this means that alerts will be generated faster and it will be easier to track down the offending revision.

A final note, Mozilla-Beta is a branch where little development takes place. The volume is really low and alerts come 5 days (or more) later. It is important to address Mozilla-Beta alerts ASAP because that is what we are shipping to customers.

### What is coalescing?

Coalescing is a term we use for when we schedule jobs to run on a given machine. When the load is high these jobs are placed in a queue and the longer the queue we skip over some of the jobs. This allows us to get results on more recent changesets faster.

This affects Talos numbers as we see regressions which show up over >1 changeset that is pushed. We have to manually fill in the coalesced jobs (including builds sometimes) to ensure we have the right changeset to blame for the regression.

Some things to be aware of:

- missing test jobs - This could be as easy as waiting for jobs to finish, or scheduling the missing job assuming it was coalesced, otherwise, it could be a missing build.
- missing builds - we would have to generate builds, which automatically schedules test jobs, sometimes these test jobs are coalesced and not run.
- results might not be possible due to build failures, or test failures
- [pgo builds](#what-is-pgo) are not coalesced, they just run much less frequently. Most likely a pgo build isn't the root cause

Here is a view on treeherder of missing data (usually coalescing):

```{image} ./Coalescing_markedup.png
:align: center
:alt: Coalescing_markedup
```

Note the two pushes that have no data (circled in red). If the regression happened around here, we might want to backfill those two jobs so we can ensure we are looking at the push which caused the regression instead of >1 push.

### What is an uplift?

Every [6 weeks](https://whattrainisitnow.com/calendar/) we release a new version of Firefox. When we do that, our code which developers check into the nightly branch gets uplifted (thing of this as a large [merge](#what-is-a-merge)) to the Beta branch. Now all the code, features, and Talos regressions are on Beta.

This affects the Performance Sheriffs because we will get a big pile of alerts for Mozilla-Beta. These need to be addressed rapidly. Luckily almost all the regressions seen on Mozilla-Beta will already have been tracked on Mozilla-Inbound or Autoland.

### What is a merge?

Many times each day we merge code from the integration branches into the main branch and back. This is a common process in large projects. At Mozilla, this means that the majority of the code for Firefox is checked into Mozilla-Inbound and Autoland, then it is merged into Mozilla-Central (also referred to as Firefox) and then once merged, it gets merged back into the other branches. If you want to read more about this merge procedure, here are [the details](https://wiki.mozilla.org/Sheriffing/How_To/Merges).

```{image} ./Merge.png
:align: center
:alt: Merge
```

Note that the topmost revision has the commit messsage of: "merge m-c to m-i". This is pretty standard and you can see that there are a series of [changesets](https://hg-edge.mozilla.org/integration/mozilla-inbound/pushloghtml?changeset=126a1ec5c7c5), not just a few related patches.

How this affects alerts is that when a regression lands on Mozilla-Inbound, it will be merged into Firefox, then Autoland. Most likely this means that you will see duplicate alerts on the other integration branch.

- note: we do not generate alerts for the Firefox (Mozilla-Central) branch.

### What is a backout?

Many times we backout or hotfix code as it is causing a build failure or unittest failure. The [Sheriff team](https://wiki.mozilla.org/Sheriffing/Sheriff_Duty) handles this process in general and backouts/hotfixes are usually done within 3 hours (i.e. we won't have [12 future changesets](#why-do-we-need-12-future-data-points)) of the original fix. As you can imagine we could get an alert 6 hours later and go to look at the graph and see there is no regression, instead there is a temporary spike for a few data points.

While looking on TreeHerder for a backout, they all mention a backout in the commit message:

```{image} ./Backout_tree.png
:align: center
:alt: Backout_tree
```

- note ^ the above image mentions the bug that was backed out, sometimes it is the revisoin.

Backouts which affect [Perfherder alerts](https://wiki.mozilla.org/TestEngineering/Performance/Sheriffing/Alerts) always generate a set of improvements and regressions. These are usually easy to spot on the graph server and we just need to annotate the set of alerts for the given revision with the bug to track what took place, see [Backout/Regression-fix Alerts](#292-backoutregression-fix-alerts).

Here is a view on graph server of what appears to be a backout (it could be a fix that landed quickly also):

```{image} ./Backout_graph.png
:align: center
:alt: Backout_graph
```

### What is PGO?

PGO is Profile Guided Optimization [Profile Guided Optimization](https://wiki.mozilla.org/TestEngineering/Performance/Sheriffing/Alerts) where we do a build, run it to collect metrics and optimize based on the output of the metrics. We only release PGO builds, and for the integration branches we do these periodically (6 hours) or as needed. For Mozilla-Central we follow the same pattern. As the builds take considerably longer (2+ times as long) we don't do this for every commit into our integration branches.

How does this affect alerts? We care most about PGO alerts- that is what we ship! Most of the time an alert will be generated for a -Non-PGO build and then a few hours or a day later we will see alerts for the PGO build.

Pay close attention to the branch the alerts are on, most likely you will see it on the non-pgo branch first (i.e. Mozilla-Inbound-Non-PGO), then roughly a day later you will see a similar alert show up on the PGO branch (i.e. Mozilla-Inbound).

Caveats:

- OSX does not do PGO builds, so we do not have -Non-PGO branches for those platforms. (i.e. we only have Mozilla-Inbound)
- PGO alerts will probably have different regression percentages, but the overall list of platforms/tests for a given revision will be almost identical

### What alerts are displayed in Alert Manager?

Perfherder [alerts](https://treeherder.mozilla.org/perf.html#/alerts) defaults to [multiple types of alerts](https://wiki.mozilla.org/TestEngineering/Performance/Sheriffing/Alerts) that are untriaged. It is a goal to keep these lists empty! You can view alerts that are improvements or in any other state (i.e. investigating, fixed, etc.) by using the drop down at the top of the page.

### Do we care about all alerts/tests?

Yes we do. Some tests are more commonly invalid, mostly due to the noise in the tests. We also adjust the threshold per test, the default is 2%, but for Dromaeo it is 5%. If we consider a test too noisy, we consider removing it entirely.

Here are some platforms/tests which are exceptions about what we run:

- Linux 64bit - the only platform which we run dromaeo_dom
- Linux 32/64bit - the only platform in which no [platform_microbench](https://wiki.mozilla.org/TestEngineering/Performance/Sheriffing/Alerts#platform_microbench) test runs, due to high noise levels
- Windows 7 - the only platform that supports xperf (toolchain is only installed there)
- Windows 7/10 - heavy profiles don't run here, because they take too long while cloning the big profiles; these are tp6 tests that use heavy user profiles

Lastly, we should prioritize alerts on the Mozilla-Beta branch since those are affecting more people.

### What does a regression look like on the graph?

On almost all of our tests, we are measuring based on time. This means that the lower the score the better. Whenever the graph increases in value that is a regression.

Here is a view of a regression:

```{image} ./Regression.png
:align: center
:alt: Regression
```

We have some tests which measure internal metrics. A few of those are actually reported where a higher score is better. This is confusing, but we refer to these as reverse tests. The list of tests which are reverse are:

- canvasmark
- dromaeo_css
- dromaeo_dom
- rasterflood_gradient
- speedometer
- tcanvasmark
- v8 version 7

Here is a view of a reverse regression:

```{image} ./Reverse_regression.png
:align: center
:alt: Reverse_regression
```

### Why does Alert Manager print -xx% ?

The alert will either be a regression or an improvement. For the alerts we show by default, it is regressions only. It is important to know the severity of an alert. For example a 3% regression is important to understand, but a 30% regression probably needs to be fixed ASAP. This is annotated as a XX% in the UI. there are no + or - to indicate improvement or regression, this is an absolute number. Use the bar graph to the side to determine which type of alert this is.

NOTE: for the reverse tests we take that into account, so the bar graph will know to look in the correct direction.

### What is noise?

Generally a test reports values that are in a range instead of a consistent value. The larger the range of 'normal' results, the more noise we have.

Some tests will post results in a small range, and when we get a data point significantly outside the range, it is easy to identify.

The problem is that many tests have a large range of expected results (we call them unstable). It makes it hard to determine what a regression is when we might have a range += 4% from the median and we have a 3% regression. It is obvious in the graph over time, but hard to tell until you have many future data points.

```{image} ./Noisy_graph.png
:align: center
:alt: Noisy_graph
```

### What are low value tests?

In the context of noise, the low value mean that the regression magnitude is too small related to the noise of the tests, thus it's pretty hard to tell which particular bug/commit caused this, but rather a range.
In a sheriffing perspective, those often end up as WONTFIX/INVALID or tests which are often considered unreliable, not relevant to current Firefox revision etc.

```{image} ./Noisy_low_value_graph.png
:align: center
:alt: Noisy_low_value_graph
```

### Why can we not trust a single data point?

This is a problem we have dealt with for years with no perfect answer. Some reasons we do know are:

- the test is noisy due to timing, diskIO, etc.
- the specific machine might have slight differences
- sometimes we have longer waits starting the browser or a pageload hang for a couple extra seconds

The short answer is we don't know and have to work within the constraints we do know.

### Why do we need 12 future data points?

We are re-evaluating our assertions here, but the more data points we have, the more confidence we have in the analysis of the raw data to point out a specific change.

This causes problem when we land code on Mozilla-Beta and it takes 10 days to get 12 data points. We sometimes rerun tests and just retriggering a job will help provide more data points to help us generate an alert if needed.

### Can't we do smarter analysis to reduce noise?

Yes, we can. We have other projects and a masters thesis [masters thesis](https://wiki.mozilla.org/images/c/c0/Larres-thesis.pdf) has been written on this subject. The reality is we will still need future data points to show a trend and depending on the source of data we will need to use different algorithms to analyze it.

### How can duplicate alerts can be identified?

One problem with [coalescing](#what-is-coalescing) is that we sometimes generate an original alert on a range of changes, then when we fill in the data (backfilling/retriggering) we generate new alerts. This causes confusion while looking at the alerts.

Here are some scenarios which duplication will be seen:

- backfilling data from [coalescing](#what-is-coalescing), you will see a similar alert on the same branch/platform/test but a different revision
  : - action: reassign the alerts to the original alert summary so all related alerts are in one place!
- we merge changesets between branches
  : - action: find the original alert summary on the upstream branch and mark the specific alert as downstream to that alert summary
- [pgo](#what-is-pgo) builds
  : - action: reassign these to the non-pgo alert summary (if one exists), or downstream to the correct alert summary if this originally happened on another branch

In Alert Manager it is good to acknowledge the alert and use the reassign or downstream actions. This helps us keep track of alerts across branches whenever we need to investigate in the future.

#### Which alert summaries can be set as downstream to autoland alerts?

Changes land on autoland first and are later merged to other branches, so alert summaries generated on the following branches can be set as downstream to the autoland alert summary that originally caught the regression or improvement:

- mozilla-central
- mozilla-beta
- mozilla-release

For mozilla-beta and mozilla-release alerts, the change usually reached the branch through a merge or an uplift several weeks after it landed on autoland, so search autoland for the original alert summary using the test, platform and magnitude of the alert rather than the push date.

### What are weekend spikes?

On weekends (Saturday/Sunday) and many holidays, we find that the volume of pushes are much smaller. This results in much fewer tests to be run. For many tests, especially ones that are noisier than others, we find that the few data points we collect on a [weekend are much less noisy](https://elvis314.wordpress.com/2014/10/30/a-case-of-the-weekends/) (either falling to the top or bottom of the noise range).

Here is an example view of data that behaves differently on weekends:

```{image} ./Weekends_example.png
:align: center
:alt: Weekends_example
```

This affects the Talos Sheriff because on Monday when our volume of pushes picks up, we get a larger range of values. Due to the way we calculate a regression, it means that we see a shift in our expected range on Monday. Usually these alerts are generated Monday evening/Tuesday morning. These are typically small regressions (\<3%) and on the noisier tests.

### What is a multi-modal test?

Many tests are bi-modal or multi-modal. This means that they have a consistent set of values, but 2 or 3 of them. Instead of having a bunch of scattered values between the low and high, you will have 2 values, the lower one and the higher one.

Here is an example of a graph that has two sets of values (with random ones scattered in between):

```{image} ./Modal_example.png
:align: center
:alt: Modal_example
```

This affects the alerts and results because sometimes we get a series of results that are less modal than the original- of course this generates an alert and a day later you will probably see that we are back to the original x-modal pattern as we see historically. Some of this is affected by the weekends.

### What is random noise?

Random noise are the data-points that don't fit in the graph trend of the test. They happen because of various uncontrollable factors (and this is assumed) or because the test is unstable.

### How do I identify the current firefox release meta-bug?

To easily track all the regressions opened, for every Firefox release is created a meta-bug that will depend on the regressions open.

```{image} ./Advanced_search.png
:align: center
:alt: Advanced_search
```

To find all the Firefox release meta-bugs you just have to search in Advanced search for bugs with:

```{image} ./Firefox_70_meta.png
:align: center
:alt: SFirefox_70_meta
```

**Product:** Testing
**Component:** Performance
**Summary:** Contains all of the strings [meta] Firefox, Perfherder Regression Tracking Bug You can leave the rest of the fields as they are.

```{image} ./Advanced_search_filter.png
:align: center
:alt: Advanced_search_filter
```

**Result:**

```{image} ./Firefox_metabugs.png
:align: center
:alt: Firefox_metabugs
```

### How do I search for an already open regression?

Sometimes treeherder include alerts related to a test in the same summary, sometimes it doesn’t. To make sure that the regression you found doesn’t have already a bug open, you have to search in the current Firefox release meta-bug for regressions open with the summary similar to the summary of your alert. Usually, if the test name matches, it might be what you’re looking for. But, be careful, if the test name matches that doesn’t mean that it is what you’re looking for. You need to check it thoroughly.

Those situations appear because a regression appears first on one repo (e.g. autoland) and it takes a few days until the causing commit gets merged to other repos (inbound, beta, central).

### How do I follow up on already open regressions open by me?

You can follow up on all the open regression bugs created by you by searching in [Advanced search](https://bugzilla.mozilla.org/query.cgi?format=advanced) for bugs with:
**Summary:** contains all of the strings > regression on push

**Status:** NEW, ASSIGNED, REOPENED

```{image} ./Advanced_search_for_perf_regressions.png
:align: center
:alt: Advanced_search_for_perf_regressions
```

**Keywords:** perf, perf-alert, regression

**Type:** defect

```{image} ./Advanced_search_for_perf_regressions_type.png
:align: center
:alt: Advanced_search_for_perf_regressions_type
```

**Search by People:** The reporter is > [your email]

```{image} ./Advanced_search_for_perf_regressions_by_people.png
:align: center
:alt: Advanced_search_for_perf_regressions_by_people
```

And you will get the list of all open regressions reported by you:

```{image} ./Advanced_search_results.png
:align: center
:alt: Advanced_search_results
```

### How can I do a bisection?

If you're investigating a regression/improvement but for some reason it happened in a revision interval where the jobs aren't able to run or the revision contains multiple commits (this happens more often on mozilla-beta), you need to do a bisection in order to find the exact culprit. We usually adopt the binary search method. Say you have the revisions:

- abcde1 - first regressed/improved value
- abcde2
- abcde3
- abcde4
- abcde5 - last good value

Bisection steps:

1. Fetch the branch you're investigating, for example `git fetch origin autoland`.
2. Push the last good revision to try as the baseline:
   - `git checkout abcde5`
   - `./mach try fuzzy --full -q=^investigated-test-signature -m=baseline_abcde5_alert_######` (the `baseline` keyword marks the push containing the reference value)
3. Push the revision in the middle of the interval:
   - `git checkout abcde3`
   - Let's assume that abcde4 broke the tests. You need to revert it in order to get the values of your investigated test on try: `git revert abcde4`
   - `./mach try fuzzy --full -q=^investigated-test-signature -m=abcde3_alert_######` (the `baseline` keyword is only included in the reference push message)
   - Use [perfcompare](https://perf.compare/) to compare the 2 pushes.
4. If the try values of abcde3 don't include the delta compared to abcde5, then abcde1 or abcde2 are the suspects, so repeat step 3 for them to find out.
