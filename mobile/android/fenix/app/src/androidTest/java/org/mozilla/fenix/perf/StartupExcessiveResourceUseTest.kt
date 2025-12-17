/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.perf

import android.view.View
import android.view.ViewGroup
import androidx.constraintlayout.widget.ConstraintLayout
import androidx.core.view.children
import androidx.recyclerview.widget.RecyclerView
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.uiautomator.UiDevice
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.HomeActivityTestRule

// BEFORE CHANGING EXPECTED_* VALUES, PLEASE READ THE TEST CLASS KDOC.

/**
 * The number of times a StrictMode violation is suppressed during this start up scenario.
 * Incrementing the expected value indicates a potential performance regression.
 *
 * One feature of StrictMode is to detect potential performance regressions and, in particular, to
 * detect main thread IO. This includes network requests (which can block for multiple seconds),
 * file read/writes (which generally block for tens to hundreds of milliseconds), and file stats
 * (like most SharedPreferences accesses, which block for small amounts of time). Main thread IO
 * should be replaced with a background operation that posts to the main thread when the IO request
 * is complete.
 *
 * Say no to main thread IO! 🙅
 */
private const val EXPECTED_SUPPRESSION_COUNT = 12

/**
 * The number of times we call the `runBlocking` coroutine method on the main thread during this
 * start up scenario. Increment the expected values indicates a potential performance regression.
 *
 * runBlocking indicates that we're blocking the current thread waiting for the result of another
 * coroutine. While the main thread is blocked, 1) we can't handle user input and the user may feel
 * Firefox is slow and 2) we can't use the main thread to continue initialization that must occur on
 * the main thread (like initializing UI), slowing down start up overall. Blocking calls should
 * generally be replaced with a slow operation on a background thread launching onto the main thread
 * when completed. However, in a very small number of cases, blocking may be impossible to avoid.
 */
private val EXPECTED_RUNBLOCKING_RANGE = 0..2 // CI has +1 counts compared to local runs: increment these together

/**
 * The number of `ConstraintLayout`s we inflate that are children of a `RecyclerView` during this
 * start up scenario. Incrementing the expected value indicates a potential performance regression.
 * THIS IS AN EXPERIMENTAL METRIC and we are not yet confident reducing this count will mitigate
 * start up regressions. If you do not find it useful or if it's too noisy, you can consider
 * removing it.
 *
 * ConstraintLayout is expensive to inflate (though fast to measure/layout) so we want to avoid
 * creating too many of them synchronously during start up. Generally, these should be inflated
 * asynchronously or replaced with cheaper layouts (if they're not too expensive to measure/layout).
 * If the view hierarchy uses Jetpack Compose, switching to that is also an option.
 */
private val EXPECTED_RECYCLER_VIEW_CONSTRAINT_LAYOUT_CHILDREN =
    4..6 // The messaging framework is not deterministic and could add to the count.

/**
 * The number of layouts we inflate during this start up scenario. Incrementing the expected value
 * indicates a potential performance regression. THIS IS AN EXPERIMENTAL METRIC and we are not yet
 * confident reducing this count will mitigate start up regressions. If you do not find it useful or
 * if it's too noisy, you can consider removing it.
 *
 * Each layout inflation is suspected of having overhead (e.g. accessing each layout resource from
 * disk) so suspect inflating more layouts may slow down start up. Ideally, layouts would be merged
 * such that there is one inflation that includes all of the views needed on start up.
 */
private val EXPECTED_NUMBER_OF_INFLATION =
    13..14 // The messaging framework is not deterministic and could add a +1 to the count

private val failureMsgStrictMode = getErrorMessage("StrictMode suppression")
private val failureMsgRunBlocking = getErrorMessage("runBlockingIncrement")
private val failureMsgRecyclerViewConstraintLayoutChildren = getErrorMessage(
    "ConstraintLayout being a common direct descendant of a RecyclerView",
)
private val failureMsgNumberOfInflation = getErrorMessage("start up inflation")

/**
 * A performance test that attempts to minimize start up performance regressions using heuristics
 * rather than benchmarking. These heuristics measure occurrences of known performance anti-patterns
 * and fails when the occurrence count changes. If the change indicates a regression, we should
 * re-evaluate the PR to see if we can avoid the potential regression and, if not, change the
 * expected value. If it indicates an improvement, we can change the expected value. The expected
 * values can be updated without consulting the performance team.
 *
 * See `EXPECTED_*` above for explanations of the heuristics this test currently supports.
 *
 * The benefits of a heuristics-based performance test are that it is uses less CI time to get
 * results so we can run it more often (e.g. for each PR) and it is less noisy than a benchmark.
 * However, the downsides of this style of test is that if a heuristic value increases, it may not
 * represent a real, significant performance regression.
 */
class StartupExcessiveResourceUseTest {
    @get:Rule
    val activityTestRule = HomeActivityTestRule(skipOnboarding = true)

    private val uiDevice = UiDevice.getInstance(InstrumentationRegistry.getInstrumentation())

    @Test
    fun verifyRunBlockingAndStrictModeSuppressionCount() {
        uiDevice.waitForIdle() // wait for async UI to load.

        // This might cause intermittent: at an arbitrary point after start up (such as the visual
        // completeness queue), we might run code on the main thread that suppresses StrictMode,
        // causing this number to fluctuate depending on device speed. We'll deal with it if it occurs.
        val actualSuppressionCount = activityTestRule.activity.components.strictMode.suppressionCount.get().toInt()
        val actualRunBlocking = RunBlockingCounter.count.get()

        assertEquals(failureMsgStrictMode, EXPECTED_SUPPRESSION_COUNT, actualSuppressionCount)
        assertTrue(failureMsgRunBlocking + "actual: $actualRunBlocking", actualRunBlocking in EXPECTED_RUNBLOCKING_RANGE)

        // This below asserts fail in Firebase with different values for
        // "actualRecyclerViewConstraintLayoutChildren" or "actualNumberOfInflations"
        // See https://github.com/mozilla-mobile/fenix/pull/26512 and https://github.com/mozilla-mobile/fenix/issues/25142
        //
        // val rootView = activityTestRule.activity.findViewById<LinearLayout>(R.id.rootContainer)
        // val actualRecyclerViewConstraintLayoutChildren = countRecyclerViewConstraintLayoutChildren(rootView, null)
        // assertTrue(
        //     failureMsgRecyclerViewConstraintLayoutChildren + "actual: $actualRecyclerViewConstraintLayoutChildren",
        //     actualRecyclerViewConstraintLayoutChildren in EXPECTED_RECYCLER_VIEW_CONSTRAINT_LAYOUT_CHILDREN,
        // )
        // val actualNumberOfInflations = InflationCounter.inflationCount.get()
        // assertTrue(
        //     failureMsgNumberOfInflation + "actual: $actualNumberOfInflations",
        //     actualNumberOfInflations in EXPECTED_NUMBER_OF_INFLATION,
        // )
    }
}

private fun countRecyclerViewConstraintLayoutChildren(view: View, parent: View?): Int {
    val viewValue = if (parent is RecyclerView && view is ConstraintLayout) {
        1
    } else {
        0
    }

    return if (view !is ViewGroup) {
        viewValue
    } else {
        viewValue + view.children.sumOf { countRecyclerViewConstraintLayoutChildren(it, view) }
    }
}

private fun getErrorMessage(shortName: String) = """$shortName count does not match expected count.

    This heuristic-based performance test is expected measure the number of occurrences of known
    performance anti-patterns and fail when that count changes. Please read the class documentation
    for more details about this test and an explanation of what the failed heuristic is expected to
    measure. Please consult the performance team if you have questions.

"""
