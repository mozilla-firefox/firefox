/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.helpers

import android.util.Log
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2
import androidx.test.espresso.Espresso
import androidx.test.espresso.base.DefaultFailureHandler
import androidx.test.platform.app.InstrumentationRegistry
import leakcanary.NoLeakAssertionFailedError
import mockwebserver3.MockWebServer
import org.junit.After
import org.junit.Before
import org.junit.Rule
import org.junit.rules.TestRule
import org.junit.rules.TestWatcher
import org.junit.runner.Description
import org.junit.runners.model.Statement
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.IdlingResourceHelper.unregisterAllIdlingResources
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.helpers.TestHelper.exitMenu
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.efficiency.logging.AttemptFinalization
import org.mozilla.fenix.ui.efficiency.logging.DiagnosticFaultInjection
import org.mozilla.fenix.ui.efficiency.logging.TestLogging
import org.mozilla.fenix.ui.efficiency.logging.TestStatus
import org.mozilla.fenix.ui.efficiency.logging.TimedReporter
import org.mozilla.fenix.ui.efficiency.navigation.LaunchConfig
import org.mozilla.fenix.ui.efficiency.navigation.PageCatalog

/**
 * What every efficiency test starts with, and who provides it.
 *
 * The setup a test inherits was spread across three places and stated in none, which is how a reader ends up with the
 * wrong answer about what leaks. This is that list. It is deliberately exhaustive: if something is not here, no test
 * should be relying on it.
 *
 * ## The rules, outermost first
 *
 * A LOWER `order` is applied further out, so this is also the order they run in.
 *
 * -2 [finalizeAttempt] emits the terminal attempt record after every other rule -1 [sampleOnArrival] records what the
 * device arrived carrying, before anything clears it 0 [EfficiencyTestRule] applies the declared environment and
 * mock-server requirements 1 [composeRuleWithCleanup] establishes the app-data boundary, launches the activity, and
 * captures failures 2 [recordTestBoundaries] records the result and state 3 [memoryLeaksRule] performs the leak
 * assertion when the run sets the detect-leaks argument
 *
 * ## What is cleared, and by which of them
 *
 * [EfficiencyTestRule] owns device and package-external setup without inheriting `FenixTestRule` or `TestSetupRule`.
 * Its behavior comes from [EfficiencyExecutionRequirements] and is recorded before the activity launches.
 *
 * [AppDataCleaner] at order 1 owns the enforced app-data boundary: its allowlisted test preferences, bookmarks, pinned
 * sites, history, site permissions, sessions, autofill addresses and cards, saved logins, tabs, in-memory downloads,
 * and launcher icon aliases. It runs at both ends and fails the boundary if any step fails. [RuntimeStateCleaner],
 * [InputStateCleaner], and [ActivityStateCleaner] own process-global UI flags, input, and activity/task teardown.
 *
 * ## What is NOT cleared by either
 *
 * The Gecko profile, which is not under /data/data at all --- it lives in app-scoped external storage and holds
 * cookies, HSTS state, certificates, Gecko-side site permissions, localStorage and IndexedDB. Also: the default search
 * engine metadata, preference keys outside the harness allowlist, Nimbus, Glean, and process memory not named by the
 * runtime cleaner. Under `am instrument` the process is shared across a class's methods, so every unnamed in-memory
 * resource can cross between them.
 *
 * The full account, with what reads each layer, is in mission-control/docs/STATE-BOUNDARIES.md.
 *
 * ## Logging
 *
 * A test describes WHAT is being tested; the harness owns HOW it runs --- navigation, selectors, waiting --- and
 * therefore owns the observability for those. Two streams come out of that: a human one on the `Eff` tag and structured
 * records on `EffJson`. Four tools outside this repository parse them, so the shape is a contract and lives in
 * logging/log-format.json.
 */
abstract class BaseTest(
    private val defaultLaunchConfig: LaunchConfig = LaunchConfig(),
    private val defaultExecutionRequirements: EfficiencyExecutionRequirements = EfficiencyExecutionRequirements(),
) {

    private companion object {
        /** Espresso's failure handler is a process-wide singleton; installing it is a one-off. */
        @Volatile var espressoHandlerInstalled = false
    }

    /**
     * How this test launches the app.
     *
     * One mechanism, not two. BaseTest used to take the seven flags as separate constructor arguments AND expose this
     * override, so the flag list existed in two places and a reader had to check both to know what a test launches
     * with. Adding a flag meant changing a constructor signature every subclass inherits.
     *
     * Pass a LaunchConfig for a fixed launch, override this for one that varies per case --- the reachability shards
     * pick theirs from the case they were given.
     */
    protected open fun launchConfig(): LaunchConfig = defaultLaunchConfig

    protected open fun executionRequirements(description: Description): EfficiencyExecutionRequirements =
        defaultExecutionRequirements

    /**
     * What the device arrived carrying, sampled before anything clears it.
     *
     * Order -1 keeps this outside environment and app-data setup. Sampling anywhere inside those rules reads what
     * survived cleanup, not what the worker or previous test delivered. The raw arrival sample is evidence only; the
     * enforced isolation sample is taken after the harness's own cleanup.
     */
    @get:Rule(order = -1)
    val sampleOnArrival: TestRule = TestRule { base, description ->
        object : Statement() {
            override fun evaluate() {
                StateProbe.record("arrival", description.displayName)
                base.evaluate()
            }
        }
    }

    private val attemptFinalization = AttemptFinalization()
    private val efficiencyTestRule =
        EfficiencyTestRule(::executionContextFor, attemptFinalization::recordCleanupFailure)

    @get:Rule(order = 0) val executionRule: TestRule = efficiencyTestRule

    protected val mockWebServer: MockWebServer
        get() = efficiencyTestRule.mockWebServer

    // Built per test rather than declared as a plain @get:Rule, because the activity flags come
    // from launchConfig(), which subclasses override --- so the config has to be read before the
    // rule is constructed. AndroidComposeTestRule also holds a TestScope that can only be entered
    // once, so it could not be reused across tests even if the flags were fixed.
    private var _composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>? = null
    private var _pageContext: PageContext? = null
    private var _executionContext: EfficiencyExecutionContext? = null

    @get:Rule(order = -2)
    val finalizeAttempt: TestRule = TestRule { base, description ->
        object : Statement() {
            override fun evaluate() {
                executionContextFor(description)
                val reporter = installedReporter()
                reporter.reset()
                attemptFinalization.reset()
                var finalFailure: Throwable? = null
                try {
                    base.evaluate()
                } catch (t: Throwable) {
                    finalFailure = t
                } finally {
                    attemptFinalization.finishCleanup()
                    runCatching {
                        reporter.record(
                            "attemptEnd",
                            attemptFinalization.terminalFields(description.displayName, finalFailure),
                        )
                    }
                }
                _executionContext = null
                finalFailure?.let { throw it }
            }
        }
    }

    /**
     * The Compose rule for the test currently running.
     *
     * Reading this before the rule at order 1 has built it is a programming error, and it used to be a `!!` --- a
     * NullPointerException with no explanation, from a rule ordering that is nowhere written down. The same hazard
     * already cost one silent bug: a watcher at order 2 recorded state through a reporter that a later @Before had not
     * installed yet, so the first test of every class lost its sample and nothing said so.
     *
     * So it says what went wrong instead. Anything that runs outside a test body --- a watcher, a rule at a lower
     * order, the inspector --- is on the wrong side of this and should not be reaching for it.
     */
    val composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>
        get() =
            checkNotNull(_composeRule) {
                "composeRule read before the activity rule built it. Rules run outermost-first by " +
                    "ascending order: -2 finalizes the attempt, -1 samples arrival, 0 is " +
                    "the execution rule, 1 builds this, and 2 records boundaries. Anything at a lower " +
                    "order than 1, or outside a test body, runs before this exists."
            }

    // There is deliberately no retry here. Firebase re-runs a failing test once
    // (num-flaky-test-attempts in the TAE flank configs), and under Gradle the AndroidX Test
    // Orchestrator gives every test its own process with package data cleared. An in-process retry
    // would be the one thing that escapes that isolation: the second attempt inherits whatever the
    // first left behind, so it can pass for the wrong reason or fail differently. See bug 2065120.
    //
    // Note that the orchestrator is a GRADLE-path guarantee, not a universal one. The fleet
    // dispatches with `am instrument`, where there is no orchestrator and no clearPackageData, so
    // a class's methods share one process and one data directory --- verified by a download added
    // to the BrowserStore in one method still being there at the start of the next. Whatever
    // isolation a test needs on that path comes from the reset between queue items and from the
    // cleanup in this file, not from the runner.
    @get:Rule(order = 1)
    val composeRuleWithCleanup: TestRule = TestRule { base, description ->
        object : Statement() {
            override fun evaluate() {
                val lifecycleTrace = ActivityLifecycleTrace.start(description.displayName)
                try {
                    val cfg = currentExecutionContext.launchConfig
                    _pageContext = null
                    requireAppDataCleanup("before", description.displayName)
                    RuntimeStateCleaner.restore("before", description.displayName)
                    StateProbe.assertIsolated("afterSetup", description.displayName)
                    RuntimePermissionRequirements.assertSatisfied(description)
                    _composeRule =
                        AndroidComposeTestRuleV2(
                            HomeActivityIntentTestRule(
                                skipOnboarding = cfg.skipOnboarding,
                                isPageLoadTranslationsPromptEnabled = cfg.isPageLoadTranslationsPromptEnabled,
                                isPocketEnabled = cfg.isPocketEnabled,
                                isBookmarksHomeFeatureEnabled = cfg.isBookmarksHomeFeatureEnabled,
                                isRecentlyVisitedFeatureEnabled = cfg.isRecentlyVisitedFeatureEnabled,
                                shouldUseExpandedToolbar = cfg.shouldUseExpandedToolbar,
                                isTabStripEnabled = cfg.isTabStripEnabled,
                                shakeToSummarizeFeatureFlagEnabled = cfg.shakeToSummarizeFeatureFlagEnabled,
                            )
                        ) {
                            it.activity
                        }
                    try {
                        // Dump on ANY failure, not only those raised through a page-object verb.
                        // BasePage.dumpFailure covers navigateToPage and mozVerifyElementsByGroup, but
                        // a page object asserting with a bare JUnit assertTrue --- BrowserPage
                        // .verifyPageContent, for one --- throws straight past it, and the failure that
                        // most needs a picture is the one nobody wrote a verb for.
                        //
                        // Wrapped INSIDE the activity rule on purpose. Catching further out photographs
                        // a black screen: the activity rule finishes the activity in its own teardown,
                        // which runs before the exception reaches anything outside it.
                        val dumpOnFailure =
                            object : Statement() {
                                override fun evaluate() {
                                    var testFailure: Throwable? = null
                                    try {
                                        ActivityEnvironment.applyOrientation(
                                            phase = "before",
                                            testId = description.displayName,
                                            requirement = currentExecutionContext.requirements.orientation,
                                        ) {
                                            composeRule.activity
                                        }
                                        base.evaluate()
                                    } catch (t: Throwable) {
                                        testFailure = t
                                        // A leak is not a screen failure. The assertion fires after
                                        // the test body has passed, so the dump would photograph a
                                        // healthy screen and the leak trace is the artifact that
                                        // matters.
                                        if (t !is NoLeakAssertionFailedError) {
                                            runCatching {
                                                ScreenDump.dumpAll(
                                                    _composeRule,
                                                    "test failed: ${description.methodName}",
                                                )
                                            }
                                                .onFailure {
                                                    Log.i(
                                                        "BaseTest",
                                                        "BaseTest: failure dump failed: ${it.message}",
                                                    )
                                                }
                                        }
                                    }

                                    attemptFinalization.beginCleanup()
                                    val cleanupFailures = mutableListOf<Throwable>()
                                    runCatching {
                                        RuntimeStateCleaner.restore("after", description.displayName)
                                        composeRule.waitForIdle()
                                    }
                                        .exceptionOrNull()
                                        ?.let {
                                            attemptFinalization.recordCleanupFailure("runtimeState")
                                            cleanupFailures += it
                                        }
                                    runCatching {
                                        InputStateCleaner.restore(
                                            "after",
                                            description.displayName,
                                            composeRule.activity,
                                        )
                                    }
                                        .exceptionOrNull()
                                        ?.let {
                                            attemptFinalization.recordCleanupFailure("inputState")
                                            cleanupFailures += it
                                        }
                                    runCatching {
                                        ActivityStateCleaner.restore(composeRule.activity, description.displayName)
                                    }
                                        .exceptionOrNull()
                                        ?.let {
                                            attemptFinalization.recordCleanupFailure("activityState")
                                            cleanupFailures += it
                                        }
                                    cleanupFailures.firstOrNull()?.let { primaryFailure ->
                                        cleanupFailures.drop(1).forEach(primaryFailure::addSuppressed)
                                        testFailure?.addSuppressed(primaryFailure)
                                    }

                                    testFailure?.let { throw it }
                                    cleanupFailures.firstOrNull()?.let { throw it }
                                }
                            }
                        var executionFailure: Throwable? = null
                        try {
                            InputStateCleaner.restore("before", description.displayName)
                            composeRule.apply(dumpOnFailure, description).evaluate()
                        } catch (t: Throwable) {
                            executionFailure = t
                        }
                        attemptFinalization.beginCleanup()
                        val appCleanupFailures = mutableListOf<Throwable>()
                        runCatching { requireAppDataCleanup("after", description.displayName) }
                            .exceptionOrNull()
                            ?.let {
                                attemptFinalization.recordCleanupFailure("appData")
                                appCleanupFailures += it
                            }
                        runCatching { StateProbe.assertIsolated("afterCleanup", description.displayName) }
                            .exceptionOrNull()
                            ?.let {
                                attemptFinalization.recordCleanupFailure("stateIsolation")
                                appCleanupFailures += it
                            }
                        appCleanupFailures.firstOrNull()?.let { primaryFailure ->
                            appCleanupFailures.drop(1).forEach(primaryFailure::addSuppressed)
                        }
                        val appCleanupFailure = appCleanupFailures.firstOrNull()
                        executionFailure?.let { failure ->
                            appCleanupFailure?.let(failure::addSuppressed)
                            throw failure
                        }
                        appCleanupFailure?.let { throw it }
                    } catch (t: NoLeakAssertionFailedError) {
                        Log.i("BaseTest", "BaseTest: NoLeakAssertionFailedError caught.")
                        cleanup(removeTabs = true)
                        throw t
                    }
                } finally {
                    runCatching { lifecycleTrace.close() }
                        .onFailure { Log.i("BaseTest", "BaseTest: lifecycle trace close failed: ${it.message}") }
                }
            }
        }
    }

    // One context per test keeps the facade tied to the current rule without rebuilding every
    // page object and re-registering the navigation graph on every `on` access.
    protected val on: PageContext
        get() = _pageContext ?: PageContext(composeRule).also { _pageContext = it }

    /**
     * Reporter lifecycle:
     * - Create once if missing.
     * - Reset at the start of every test so summaries and counters are per-test.
     *
     * Why reset instead of re-creating:
     * - Keeps construction cheap and avoids wiring churn if we later attach file sinks.
     * - Makes it easier to evolve toward a more formal "test context" object later.
     */
    /**
     * Test boundaries in the artifact stream. Without them details.jsonl is a flat run of steps with no way to say
     * which test any of them belonged to, which is the first thing triage asks.
     *
     * A watcher rather than @Before/@After because only a watcher sees the outcome.
     */
    @get:Rule(order = 2)
    val recordTestBoundaries: TestRule =
        object : TestWatcher() {
            override fun starting(description: Description) {
                // Sampled AFTER the reset and before the test body, so a non-zero count here is state
                // the reset could not reach --- somebody else's leftovers.
                StateProbe.record("start", description.displayName)
                // The launch configuration goes on the record, because "was this test even running the
                // app it expects?" is otherwise unanswerable after the fact. testStart has always taken
                // meta; nothing was filling it, so every trace claimed a default launch.
                installedReporter().testStart(description.displayName, currentExecutionContext.asMeta())
            }

            override fun succeeded(description: Description) {
                StateProbe.record("end", description.displayName)
                attemptFinalization.recordBody(TestStatus.PASS)
                installedReporter().testEnd(description.displayName, TestStatus.PASS)
                DiagnosticFaultInjection.afterPassingBody(description.displayName)
            }

            override fun failed(e: Throwable, description: Description) {
                // Especially on failure: "the store has three history entries and the screen shows
                // none" is a different bug report from "the store is empty too".
                StateProbe.record("end", description.displayName)
                attemptFinalization.recordBody(TestStatus.FAIL)
                installedReporter().testEnd(description.displayName, TestStatus.FAIL)
            }

            override fun skipped(e: org.junit.AssumptionViolatedException, description: Description) {
                attemptFinalization.recordBody(TestStatus.SKIP)
                installedReporter().testEnd(description.displayName, TestStatus.SKIP)
            }
        }

    /**
     * Leak detection for every efficiency test, gated on the `detect-leaks` runner argument.
     *
     * One rule here replaces the per-class declaration the legacy suite repeats in 56 files, so a new test is covered
     * by default and an author cannot forget it. It costs nothing when the argument is absent: [DetectMemoryLeaksRule]
     * returns the unwrapped statement, which is what every task except the one that sets the flag gets --- including
     * the generated shard suites, which inherit this class.
     *
     * Order 3 is forced from both directions. It has to be above 1 so the Compose rule that [composeRuleWithCleanup]
     * builds is still active for the `waitForIdle()` drain the assertion depends on --- at order 1 or below,
     * [composeRule] has not been built and reading it throws. Nothing is nested inside it, so the heap is sampled as
     * soon as the test body and its `@After` methods return, which is before any of the cleaners in
     * [composeRuleWithCleanup] run: tabs, app data and activity state are all still in place at that point.
     *
     * A failure here is a [NoLeakAssertionFailedError], caught by [composeRuleWithCleanup] for cleanup and re-thrown.
     * The leak trace is written to a file and pulled as a task artifact; see FenixDetectLeaksAssert.
     */
    @get:Rule(order = 3) val memoryLeaksRule = DetectMemoryLeaksRule(composeTestRule = { composeRule })

    /**
     * Silent until a test installs a real one, so page objects driven outside a test - the inspector, tooling - narrate
     * nothing rather than crashing or logging into a void. Called from both the watcher and setUp because rule ordering
     * decides which runs first and neither should care.
     */
    private fun installedReporter(): TimedReporter = TestLogging.installed()

    protected val currentExecutionContext: EfficiencyExecutionContext
        get() = checkNotNull(_executionContext) { "Execution context is unavailable outside a running test" }

    private fun executionContextFor(description: Description): EfficiencyExecutionContext {
        _executionContext?.let { existing ->
            check(existing.testId == description.displayName) {
                "Execution context for ${existing.testId} cannot run ${description.displayName}"
            }
            return existing
        }
        return EfficiencyExecutionContext(
                testId = description.displayName,
                launchConfig = launchConfig(),
                requirements = executionRequirements(description),
            )
            .also { _executionContext = it }
    }

    private fun requireAppDataCleanup(phase: String, testId: String) {
        val failed = AppDataCleaner.clear(phase, testId)
        check(failed.isEmpty()) {
            "App-data cleanup failed at $phase for $testId: ${failed.joinToString()}"
        }
    }

    /**
     * An opt-in diagnostic, read from the instrumentation arguments.
     *
     * These two were gated on `java.lang.Boolean.getBoolean`, which reads a JVM system property. Instrumentation
     * arguments do not set system properties --- they arrive through InstrumentationRegistry --- and nothing in the
     * tree calls System.setProperty for either name, so both hooks were unreachable and the page-catalog one did real
     * reflective work that nobody could trigger. Same mechanism SnapshotPrimer already uses:
     *
     * am instrument -e logPageCatalog true ...
     */
    private fun installEspressoFailureHandlerOnce() {
        if (espressoHandlerInstalled) return
        Espresso.setFailureHandler(DefaultFailureHandler(appContext, false))
        espressoHandlerInstalled = true
    }

    private fun flagArg(name: String): Boolean =
        InstrumentationRegistry.getArguments().getString(name)?.toBooleanStrictOrNull() ?: false

    @Before
    fun setUp() {
        // Disable Espresso's screenshot-on-failure locally.
        //
        // Why: Espresso's DefaultFailureHandler captures a screenshot when an interaction fails.
        // On a debug build on a REAL device, that bitmap capture (DeviceCapture ->
        // takeScreenshotOnNextFrame) trips Fenix's StrictMode penaltyDeath and KILLS the process
        // before the real assertion error is reported — so the genuine failure is swallowed and the
        // test looks like an opaque crash. It does not reproduce on Firebase (no penaltyDeath /
        // different capture path), which is why these only fail locally. Installing the default
        // handler with captureScreenshotOnFailure = false keeps failure messages intact without the
        // fatal screenshot. We still get the real error (and our own ScreenDump) for debugging.
        // Second arg is captureScreenshotOnFailure = false.
        //
        // Installed once for the process rather than on every test. It is a process-wide
        // singleton and nothing ever put it back, so setting it per test was writing the same
        // value repeatedly to a global with no owner --- and under `am instrument` the process is
        // shared across a class's methods, so "per test" was never true anyway.
        installEspressoFailureHandlerOnce()

        if (flagArg("logNavigationSummary")) {
            on
            on.navigationGraph.logPathSummary()
        }
        if (flagArg("logPageCatalog")) {
            val pages = PageCatalog.discoverPages()

            Log.i("PageCatalog", "Discovered ${pages.size} pages from PageContext")

            pages.forEachIndexed { index, pageRef ->
                val page = pageRef.getter(on)

                Log.i(
                    "PageCatalog",
                    "   ${index + 1}. ${page.pageName} (property=${pageRef.propertyName})",
                )
            }
        }

        // Where navigation believes it is. A breadcrumb, not a source of truth --- that stays
        // selector-based verification (mozIsOnPageNow / mozWaitForPageToLoad) --- but it now has a
        // second reader: ScreenDump writes it into a failure as `harnessPage`, and mission-control
        // shows it beside the page the analyser infers from the screen. A disagreement between the
        // two means navigation recorded an arrival it did not make. So a stale value is a
        // misleading diagnostic now, not just an internal inconsistency, and resetting it at the
        // start of every test is what keeps it honest.
        PageStateTracker.reset()
    }

    /**
     * Print a short per-test summary to stdout.
     *
     * Why:
     * - Helps spot where time was spent and which layer failed most often (STEP/CMD/LOC).
     * - Provides immediate value in CI where you may only have the log artifact.
     *
     * Note:
     * - "Wall time" is overall elapsed real-world time for the test (start -> end).
     * - STEP/CMD/LOC totals sum only the instrumented scopes.
     */
    @After
    fun tearDownLogging() {
        try {
            TestLogging.reporter.printSummary()
        } catch (_: Throwable) {
            // Logging must never fail a test.
        }
    }
}

private fun cleanup(removeTabs: Boolean = false) {
    unregisterAllIdlingResources()
    if (removeTabs) {
        appContext.components.useCases.tabsUseCases.removeAllTabs(recoverable = false)
    }
    exitMenu()
}
