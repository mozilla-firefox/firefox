# Perftest review checklist

Compare each item with the diff. Perftest reviewers ask about these items most often.

## 1. Try push and PerfCompare evidence

- Before you ask for review, run the pre-submit table in SKILL.md.
- Ask for a try push that runs each changed or new task on each touched platform and app, including android. `./mach try perf` is usually the easiest way to select the tasks.
- If the patch touches android, ask for `--no-artifact`. Android tasks don't run in artifact mode.
- If the numbers can change, ask for a PerfCompare against a base without the patch. `./mach try perf` pushes the base for you.
- In the subtest view, look for 0 values, missing subtests and large shifts. These usually mean that a test broke silently.
- Make sure the requested tests show up in each linked try push.
- For a stack, one try push over the whole stack is enough.

## 2. Follow-up bugs and patch scope

- Each known gap needs a bug. The code gets a comment like `# Bug XXXXXX: reason or what to do`.
- Fix a failing test, or disable it with a bug. We don't land known failures.
- Ask the author to say which try failures are unrelated to the patch.
- Put unrelated or mechanical changes in a separate patch.
- Put generated perfdocs changes in the patch that causes them.

## 3. Perfdocs

- After a change to a task, test, branch or description, run `./mach perfdocs --generate` and commit the result.
- Don't edit `testing/perfdocs/generated/` by hand.
- If the patch removes tests or changes their descriptions, update the harness perfdocs sources (`perfdocs/config.yml`, `*.md`).
- Make sure the owner and contact fields name the right team.
- In a stack, a perfdocs failure on one patch is often a change that belongs in a different patch of the stack.

## 4. Scheduling: branches, tiers, crons, capacity

- Alerting needs autoland. Tests that run only on mozilla-central or on a cron don't alert.
- Make sure `run-on-projects` matches what the patch says it wants.
- With `cron: true`, the task doesn't need `run-on-projects`.
- With `cron: false` and `run-on-projects: []`, the task never runs in CI. Make sure that is intended.
- Make sure new or moved tasks have the expected `run-on-projects`. This doesn't need a try push. Use `./mach taskgraph full` (or `target`) locally, or look in the generated perfdocs.
- If the patch doesn't say whether a new task is for sheriffing or is experimental, ask the author. Experimental tasks are generally tier 3. A task that must alert needs to run on autoland at tier 1 or 2.
- Prefer shippable builds for perf tasks.
- Tests with timing or score metrics should run on hardware workers. For tests with counting metrics (for example memory in AWSY or I/O counts in xperf), a VM can be acceptable.
- Ask about capacity: low-capacity device pools, tasks that run on every push, and copies of autoland tasks instead of conversions. A daily or weekly cron is often enough.
- After a test is removed or disabled, check each cron and target task that used it (`./mach taskgraph target`).

## 5. Shared harness code and new tests

- New tests should use the existing extension points of the harness (mozperftest layers, raptor test types), not one-off code paths.
- New raptor tests should use python support scripts (`testing/raptor/browsertime/support-scripts/`).
- A change to shared harness code, base classes or defaults changes each test that uses them. Ask for a try push of those tests on all platforms.

## 6. Android devices and pools

- Ask the author to say which devices (a55, p6, s24, ...) and apps (fenix, geckoview, chrome-m) run where, and why.
- Before tasks are added, check the capacity of the pool.
- Check whether a cron or `run-on-projects` schedules the tests of each device.
- If a problem affects the whole pool, file a bug against the pool. Don't add a workaround to each test.

## 7. Alerts and sheriff impact

- A rename of a test, subtest, option or extra option resets its Perfherder time series.
- A move to a new platform or variant needs two weeks of parallel runs. Add the new tasks, keep the old tasks for two weeks, then remove the old tasks.
- Before a patch lands, tell the perf sheriffs if it adds alerts, changes tiers or removes tests.
- Sheriffs classify alerts from recording, harness or metric changes as harness changes.
- Ask whether new metrics should alert. Then check the setting in the output (item 11).

## 8. Browser and driver versions

- We keep only the two latest chromedrivers. Tell relops before you drop one.
- Check the Chromium-as-Release cron parameters, and other cron parameters, with `./mach taskgraph target -p <parameters.yml>`.
- Branch on the app (`app == "custom-car"`), not on which folders exist.
- Make sure a disable applies only to the intended browsers.
- Make sure Safari and safaridriver exist on the target machines.

## 9. Profiling

- Profiling variants (`--extra-profiler-run`, simpleperf, native profiling) don't run on autoland or mozilla-central, unless the patch intends that.
- Make sure the name match for a profiling variant can't select unrelated tasks. For example, a test with `simple` in its name isn't a simpleperf task.
- Don't turn on `MOZ_LOG` or log modules by default. They make tests slower and the artifacts larger.

## 10. Removals reach every reference

- Search for the removed name in task kinds, transforms, crons, the fetch kind, tooltool and mitmproxy manifests, benchmark tomls, cmdline choices, perfselector entries, test fixtures and perfdocs.
- Remove a dropped version, browser, device or variant from each list in the same patch.
- Rebase onto earlier removals, and drop the changes that they made dead.

## 11. Perfherder metrics

- Suite and subtest names are 80 characters or fewer.
- Check `shouldAlert`, units and `lowerIsBetter` in the perfherder data that the test produces. The manifest alone isn't enough.
- Subtests don't alert unless `shouldAlert` is set.
- A new variant gets its own `extraOptions` entry. Then its data doesn't mix with the data of the base test.

## 12. Patch title and summary

- The title says what the patch does, as an action ("Add a perftest for ...").
- The summary says why, which platforms, and what changes for the sheriffs.
- Make sure the summary matches the code. Each flag in the summary must exist.

## 13. Python in harness code

- Use `pathlib` and `sys.executable`, not hard-coded paths.
- Don't assume that a subprocess started. Check it.
- Before you import a helper, make sure it is available in CI. For example, `mozperftest.utils` isn't available in raptor in CI.

## 14. Fetches, pinning, recordings and playback

- When benchmark sources change, update `taskcluster/kinds/fetch/benchmarks.yml` too. Add a comment in both places.
- Pin fetch URLs and requirements, so that installs are reproducible.
- Update tooltool references in the same patch as the recording manifest.
- A change to proxy or playback code affects each mitmproxy test. Ask for try pushes of those tests.
- Look at new recordings and videos for sign-in windows and pop-ups. A new recording that has them is a problem, so ask for it to be recorded again.
- If a site stays noisy, a new recording can help.

## 15. Measurement correctness and noise

- Compare the noise before and after the patch on each platform. A stable mean with a wider spread is still a regression in the test.
- Flush layout before you start the timer, and start the timer after setup.
- Don't use `requestIdleCallback` in timed code. Keep costly calls out of timed code.
- Make sure new options make sense for each test that inherits them.

## 16. Generated configs and transforms

- Check the task selection with a `parameters.yml` (`./mach taskgraph target -p`).
- Resolve `by-test-platform` keyed values before the transform that needs them.
- Remove settings that a transform overwrites.

## 17. Scope settings to the test that needs them

- Put per-test options in the section of that test. A harness-wide default changes each test.
- If a python support script is available, prefer it for per-test options.

## 18. Task runtime

- Set `max-run-time` from the try push times. If the tasks finish quickly, decrease it.
- If a task gets close to the limit, split it if possible. Otherwise, increase `max-run-time`.
