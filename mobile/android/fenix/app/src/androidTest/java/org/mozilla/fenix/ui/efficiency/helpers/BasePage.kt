/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.helpers

import android.os.SystemClock
import android.util.Log
import android.view.accessibility.AccessibilityWindowInfo
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.test.SemanticsNodeInteraction
import androidx.compose.ui.test.SemanticsNodeInteractionCollection
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertAll
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.assertIsNotSelected
import androidx.compose.ui.test.assertIsSelected
import androidx.compose.ui.test.filter
import androidx.compose.ui.test.hasAnyAncestor
import androidx.compose.ui.test.hasAnyChild
import androidx.compose.ui.test.hasAnySibling
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasParent
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.compose.ui.test.longClick as composeLongClick
import androidx.compose.ui.test.onAllNodesWithContentDescription
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onFirst
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performImeAction
import androidx.compose.ui.test.performSemanticsAction
import androidx.compose.ui.test.performTextClearance
import androidx.compose.ui.test.performTextInput
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipe
import androidx.test.espresso.Espresso.closeSoftKeyboard
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.ViewInteraction
import androidx.test.espresso.action.ViewActions.clearText
import androidx.test.espresso.action.ViewActions.click
import androidx.test.espresso.action.ViewActions.longClick
import androidx.test.espresso.action.ViewActions.pressImeActionButton
import androidx.test.espresso.action.ViewActions.swipeDown
import androidx.test.espresso.action.ViewActions.swipeLeft
import androidx.test.espresso.action.ViewActions.swipeRight
import androidx.test.espresso.action.ViewActions.swipeUp
import androidx.test.espresso.action.ViewActions.typeText
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.ViewMatchers.hasSibling
import androidx.test.espresso.matcher.ViewMatchers.isChecked
import androidx.test.espresso.matcher.ViewMatchers.isDisplayed
import androidx.test.espresso.matcher.ViewMatchers.isDisplayingAtLeast
import androidx.test.espresso.matcher.ViewMatchers.isEnabled
import androidx.test.espresso.matcher.ViewMatchers.isNotChecked
import androidx.test.espresso.matcher.ViewMatchers.isNotSelected
import androidx.test.espresso.matcher.ViewMatchers.isSelected
import androidx.test.espresso.matcher.ViewMatchers.withContentDescription
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withResourceName
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.uiautomator.By
import androidx.test.uiautomator.UiObject
import androidx.test.uiautomator.UiObject2
import androidx.test.uiautomator.UiSelector
import org.hamcrest.CoreMatchers.allOf
import org.hamcrest.CoreMatchers.not
import org.hamcrest.Matchers.containsString
import org.mozilla.fenix.helpers.AppAndSystemHelper
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.ui.efficiency.core.ComposeUiElement
import org.mozilla.fenix.ui.efficiency.core.EspressoUiElement
import org.mozilla.fenix.ui.efficiency.core.UiElement
import org.mozilla.fenix.ui.efficiency.core.UiObject2UiElement
import org.mozilla.fenix.ui.efficiency.core.UiObjectUiElement
import org.mozilla.fenix.ui.efficiency.navigation.NavigationRegistry
import org.mozilla.fenix.ui.efficiency.navigation.NavigationStep

/**
 * Logging philosophy (why BasePage owns logging):
 *
 * - Tests should be minimal, expressing *what* is being validated.
 * - Helpers/framework code is responsible for *how* actions happen (navigation, locators, retries), and that's why I
 *   think this is the correct place to instrument structured logs and timings.
 *
 * This becomes critical as we evolve toward:
 * - test factories that generate many permutations (pages x states) at runtime,
 * - CI-configurable runs (feature flags, onboarding modes, user types),
 * - reflection-based enumeration of all pages/components,
 * - and eventually AI-assisted test planning, generation, and self-healing.
 *
 * In all of those models, the structured log stream is the human-readable source of truth describing what actually
 * executed, independent of how the test was defined (code/spec/CI).
 */
abstract class BasePage(protected val composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>) {
    abstract val pageName: String

    companion object {
        // Mirrors the minimum displayed-area Espresso's click() action requires before it will tap.
        private const val CLICKABLE_VISIBILITY_PERCENT = 90
    }

    // ------------------------------------------------------------
    // Small helpers to keep messages consistent and easy to scan
    // ------------------------------------------------------------

    private fun rep() = org.mozilla.fenix.ui.efficiency.logging.TestLogging.reporter

    private fun safeId(prefix: String, raw: String): String {
        // Helps avoid super long or illegal step ids due to punctuation/spaces
        val cleaned = raw.replace(Regex("[^A-Za-z0-9_\\-]"), "_")
        return "'$prefix'_$cleaned".take(120)
    }

    private fun found(desc: String) = "'$desc' found"

    private fun notFound(desc: String) = "'$desc' not found"

    // ------------------------------------------------------------
    // Navigation (STEP)
    // ------------------------------------------------------------

    open fun navigateToPage(url: String = "", forceNavigation: Boolean = false): BasePage {
        val rep = rep()
        rep?.startStep("nav_$pageName", "Attempting to Navigate to $pageName", 0)

        try {
            if (!forceNavigation && mozIsOnPageNow()) {
                PageStateTracker.currentPageName = pageName
                rep?.endStep(success = true, message = "'$pageName' already loaded")
                return this
            }

            val fromPage = PageStateTracker.currentPageName
            Log.i("PageNavigation", "Trying to find path from '$fromPage' to '$pageName'")

            val path = NavigationRegistry.findPath(fromPage, pageName)

            if (path == null) {
                NavigationRegistry.logGraph()
                rep?.endStep(success = false, message = "No navigation path found to '$pageName'")
                throw AssertionError("No navigation path found from '$fromPage' to '$pageName'")
            } else {
                Log.i("PageNavigation", "Navigation path found from '$fromPage' to '$pageName':")
                path.forEachIndexed { i, step -> Log.i("PageNavigation", "   Step ${i + 1}: $step") }
            }

            path.forEach { step ->
                when (step) {
                    is NavigationStep.Click -> mozClick(step.selector)
                    is NavigationStep.LongClick -> mozLongClick(step.selector)
                    is NavigationStep.ClickIfPresent -> mozClickIfPresent(step.selector)
                    is NavigationStep.Swipe -> mozSwipeTo(step.selector, step.direction)
                    is NavigationStep.OpenNotificationsTray -> mozOpenNotificationsTray()
                    is NavigationStep.Action -> step.action()
                    is NavigationStep.EnterText -> mozEnterText(url, step.selector)
                    is NavigationStep.EnterTextValue -> mozEnterText(step.text, step.selector)
                    is NavigationStep.PressEnter -> mozPressEnter(step.selector)
                    is NavigationStep.PressBack -> {
                        mDevice.pressBack()
                        mDevice.waitForIdle()
                    }
                    is NavigationStep.WaitForIdle -> composeRule.waitForIdle()
                    is NavigationStep.PressBackUntilGone -> mozPressBackUntilGone(step.selector, step.maxPresses)
                }
            }

            if (!mozWaitForPageToLoad()) {
                rep?.endStep(success = false, message = "'$pageName' did not load")
                throw AssertionError("Failed to navigate to $pageName")
            }

            PageStateTracker.currentPageName = pageName
            rep?.endStep(success = true, message = "Navigation to '$pageName' completed")
            return this
        } catch (t: Throwable) {
            rep?.endStep(success = false, message = "Navigation to '$pageName' failed: ${t.message ?: "exception"}")
            // Navigation failures (esp. a page-arrival timeout on the requiredForPage anchor) previously
            // produced no screen snapshot, leaving these undebuggable. Dump the current screen so the
            // failing state (which page we actually landed on, what handles exist) is captured.
            ScreenDump.dump(composeRule, "navigateToPage failed: $pageName")
            throw t
        }
    }

    // ------------------------------------------------------------
    // Page readiness verification (CMD + LOC)
    // ------------------------------------------------------------

    private fun mozWaitForPageToLoad(timeout: Long = 10_000, interval: Long = 100): Boolean {
        val rep = rep()
        val requiredSelectors = mozGetSelectorsByGroup("requiredForPage")
        val deadline = System.currentTimeMillis() + timeout

        while (System.currentTimeMillis() < deadline) {
            rep?.startCmd("wait_$pageName", "Attempting to verify $pageName loads...", 1)

            val allPresent = requiredSelectors.all { sel ->
                rep?.startLoc(
                    safeId("loc", "${pageName}_${sel.description}"),
                    "Attempting to locate '${sel.description}'...",
                    2,
                )
                val present = mozVerifyElement(sel, applyPreconditions = false)
                rep?.endLoc(
                    success = present,
                    message = if (present) found(sel.description) else notFound(sel.description),
                )
                present
            }

            rep?.endCmd(
                success = allPresent,
                message = if (allPresent) "'$pageName' loaded" else "'$pageName' not ready yet",
            )

            if (allPresent) return true
            SystemClock.sleep(interval)
        }

        return false
    }

    /**
     * Fast "already here?" check.
     *
     * Why:
     * - We do NOT want to spend seconds waiting to verify a destination page before we even start navigating.
     * - This is intentionally a single-pass check (no polling / no sleeping).
     *
     * Pattern:
     * - navigateToPage() uses mozIsOnPageNow() first.
     * - After executing navigation steps, we use mozWaitForPageToLoad() to wait/poll for readiness.
     */
    private fun mozIsOnPageNow(): Boolean {
        val rep = rep()
        val requiredSelectors = mozGetSelectorsByGroup("requiredForPage")

        // This is a *fast check* — no retries, no sleeping.
        rep?.startCmd("is_on_'$pageName'", "Checking if '$pageName' is already visible...", 1)

        val allPresent = requiredSelectors.all { sel ->
            rep?.startLoc(
                safeId("loc", "${pageName}_${sel.description}_now"),
                "Attempting to locate '${sel.description}'...",
                2,
            )
            val found = mozVerifyElement(sel, applyPreconditions = false)
            rep?.endLoc(success = found, message = if (found) found(sel.description) else notFound(sel.description))
            found
        }

        rep?.endCmd(
            success = allPresent,
            message = if (allPresent) "'$pageName' already visible" else "'$pageName' not visible yet",
        )
        return allPresent
    }

    abstract fun mozGetSelectorsByGroup(group: String = "requiredForPage"): List<Selector>

    fun mozVerifyElementsByGroup(group: String = "requiredForPage"): BasePage {
        val rep = rep()
        rep?.startCmd(safeId("verify_group", "${pageName}_$group"), "Attempting to verify group '$group' loads...", 1)

        val selectors = mozGetSelectorsByGroup(group)
        val allPresent = selectors.all { sel ->
            rep?.startLoc(
                safeId("loc", "${pageName}_${group}_${sel.description}"),
                "Attempting to locate '${sel.description}'...",
                2,
            )
            val present = mozVerifyElement(sel, applyPreconditions = true)
            rep?.endLoc(success = present, message = if (present) found(sel.description) else notFound(sel.description))
            present
        }

        rep?.endCmd(
            success = allPresent,
            message = if (allPresent) "Group '$group' verified" else "Group '$group' missing required elements",
        )

        if (!allPresent) {
            // Dump for the same reason mozVerify does. A group failure names only the group, and the
            // per-selector log stops at the first miss (the check is an `all {}`), so without this there
            // is nothing to tell you whether the missing element is absent, renamed, or just off-screen.
            ScreenDump.dump(composeRule, "mozVerifyElementsByGroup failed: $pageName group '$group'")
            throw AssertionError("Not all elements in group '$group' are present")
        }
        return this
    }

    fun mozVerifyElementAbsent(selector: Selector): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("verify_absent", selector.description),
            "Verifying '${selector.description}' is absent...",
            1,
        )
        rep?.startLoc(safeId("loc", selector.description), "Attempting to locate '${selector.description}'...", 2)
        val present = mozVerifyElement(selector, applyPreconditions = false)
        rep?.endLoc(
            success = !present,
            message = if (!present) notFound(selector.description) else found(selector.description),
        )
        rep?.endCmd(
            success = !present,
            message =
                if (!present) "'${selector.description}' correctly absent"
                else "'${selector.description}' unexpectedly present",
        )
        if (present) throw AssertionError("Element '${selector.description}' was expected to be absent but is visible")
        return this
    }

    fun mozWaitUntilAbsent(
        selector: Selector,
        timeout: Long = TestAssetHelper.waitingTime,
        interval: Long = 500,
    ): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("wait_until_absent", selector.description),
            "Waiting until '${selector.description}' is absent...",
            1,
        )
        val deadline = System.currentTimeMillis() + timeout
        while (System.currentTimeMillis() < deadline) {
            val present = mozVerifyElement(selector, applyPreconditions = false)
            if (!present) {
                rep?.endCmd(success = true, message = "'${selector.description}' is absent")
                return this
            }
            SystemClock.sleep(interval)
        }
        rep?.endCmd(success = false, message = "'${selector.description}' still present after ${timeout}ms")
        throw AssertionError(
            "'${selector.description}' was expected to disappear but is still visible after ${timeout}ms"
        )
    }

    /**
     * Assert [selector] stays absent for the whole of [timeout], failing the moment it appears.
     *
     * Not the same as [mozVerifyElementAbsent] (a single instantaneous probe) or [mozWaitUntilAbsent] (waits for
     * something to go away). This is "must not show up", and it is sometimes load-bearing rather than defensive: a
     * screen that navigates away and bounces straight back looks absent-then-present, which only a sustained check can
     * tell apart from absent.
     */
    fun mozVerifyElementStaysAbsent(
        selector: Selector,
        timeout: Long = TestAssetHelper.waitingTimeShort,
        interval: Long = 200,
    ): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("verify_stays_absent", selector.description),
            "Verifying '${selector.description}' stays absent for ${timeout}ms...",
            1,
        )
        val deadline = System.currentTimeMillis() + timeout
        while (System.currentTimeMillis() < deadline) {
            if (mozVerifyElement(selector, applyPreconditions = false)) {
                rep?.endCmd(success = false, message = "'${selector.description}' appeared before ${timeout}ms elapsed")
                ScreenDump.dump(composeRule, "mozVerifyElementStaysAbsent failed: ${selector.description}")
                throw AssertionError(
                    "'${selector.description}' was expected to stay absent but appeared within ${timeout}ms"
                )
            }
            SystemClock.sleep(interval)
        }
        rep?.endCmd(success = true, message = "'${selector.description}' stayed absent for ${timeout}ms")
        return this
    }

    fun mozVerify(selector: Selector, timeout: Long = 5_000, interval: Long = 500): BasePage {
        val rep = rep()
        rep?.startCmd(safeId("verify", selector.description), "Verifying '${selector.description}' is present...", 1)
        val deadline = System.currentTimeMillis() + timeout
        while (System.currentTimeMillis() < deadline) {
            rep?.startLoc(safeId("loc", selector.description), "Attempting to locate '${selector.description}'...", 2)
            val present = mozVerifyElement(selector, applyPreconditions = false)
            rep?.endLoc(
                success = present,
                message = if (present) found(selector.description) else notFound(selector.description),
            )
            if (present) {
                rep?.endCmd(success = true, message = "'${selector.description}' verified")
                return this
            }
            SystemClock.sleep(interval)
        }
        // A known blocking overlay (e.g. the stylus prompt) may be covering the target — dismiss and
        // re-probe once before declaring it absent.
        if (dismissKnownOverlaysIfPresent() && mozVerifyElement(selector, applyPreconditions = false)) {
            rep?.endCmd(success = true, message = "'${selector.description}' verified after dismissing an overlay")
            return this
        }
        rep?.endCmd(success = false, message = "'${selector.description}' not found after ${timeout}ms")
        ScreenDump.dump(composeRule, "mozVerify failed: ${selector.description}")
        throw AssertionError("'${selector.description}' not found on screen after ${timeout}ms")
    }

    /**
     * Detect any known blocking overlay ([OverlayRegistry]) covering the app and dismiss it. Returns true if one was
     * DETECTED and a dismiss was attempted — not that the dismiss succeeded; callers re-probe for their own target
     * rather than trusting this. No-op (false) when none are present.
     *
     * Called automatically by mozClick/mozVerify on a locate miss so an OEM/system popup (stylus prompt, etc.) in a
     * separate window can't masquerade as "element not found". Also callable explicitly from a page-object flow that
     * knows an overlay is likely.
     *
     * Every known overlay is checked on each call, and each overlay's dismiss selectors are tried in order until the
     * overlay stops being detected. That is adequate for the single seeded overlay but has not been exercised with
     * several registered at once — see the OverlayRegistry KDoc for the open design questions before adding more.
     */
    fun dismissKnownOverlaysIfPresent(): Boolean {
        var handled = false
        for (overlay in OverlayRegistry.known) {
            if (mozVerifyElement(overlay.presence, applyPreconditions = false)) {
                Log.i("BasePage", "⚠ Blocking overlay detected: '${overlay.name}' — attempting dismiss")
                for (dismiss in overlay.dismiss) {
                    mozClickIfPresent(dismiss, timeout = 1_000)
                    // Stop at the first control that actually cleared it. Continuing would click the
                    // remaining selectors through to whatever is now underneath the dismissed overlay.
                    if (!mozVerifyElement(overlay.presence, applyPreconditions = false)) break
                }
                handled = true
            }
        }
        if (handled) composeRule.waitForIdle()
        return handled
    }

    /**
     * Is [selector] on screen right now? A probe, not an assertion: it never throws and never waits, so it can drive
     * control flow (e.g. "press back until the app screen is showing again").
     */
    fun mozIsElementPresent(selector: Selector): Boolean = mozVerifyElement(selector, applyPreconditions = false)

    /**
     * True while a soft-keyboard (IME) window is on screen. Reads the accessibility window list rather than shelling
     * out to `dumpsys input_method` the way the legacy AppAndSystemHelper does, so it needs no shell access and no
     * output parsing.
     */
    fun mozIsKeyboardVisible(): Boolean =
        InstrumentationRegistry.getInstrumentation().uiAutomation.windows.any {
            it.type == AccessibilityWindowInfo.TYPE_INPUT_METHOD
        }

    /** Poll until the soft keyboard is showing; throws if it never appears within [timeout]. */
    fun mozVerifyKeyboardVisible(timeout: Long = 5_000, interval: Long = 200): BasePage {
        val rep = rep()
        rep?.startCmd("verify_keyboard_visible", "Verifying the soft keyboard is visible...", 1)
        val deadline = System.currentTimeMillis() + timeout
        while (System.currentTimeMillis() < deadline) {
            if (mozIsKeyboardVisible()) {
                rep?.endCmd(success = true, message = "Soft keyboard is visible")
                return this
            }
            SystemClock.sleep(interval)
        }
        rep?.endCmd(success = false, message = "Soft keyboard not visible after ${timeout}ms")
        ScreenDump.dump(composeRule, "mozVerifyKeyboardVisible failed")
        throw AssertionError("Soft keyboard was expected to be visible but is not, after ${timeout}ms")
    }

    fun mozVerifyAnyContainsText(
        selector: Selector,
        text: String,
        timeout: Long = TestAssetHelper.waitingTime,
        interval: Long = 500,
    ): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("verify_any_contains_text", selector.description),
            "Verifying any '${selector.description}' contains text '$text'...",
            1,
        )
        dismissSoftKeyboard()
        val deadline = System.currentTimeMillis() + timeout
        while (System.currentTimeMillis() < deadline) {
            val match =
                mozGetAllElements(selector)
                    ?.filter(hasText(text, substring = true))
                    ?.fetchSemanticsNodes()
                    ?.isNotEmpty() == true
            if (match) {
                rep?.endCmd(success = true, message = "Found '${selector.description}' containing text '$text'")
                return this
            }
            SystemClock.sleep(interval)
        }
        rep?.endCmd(
            success = false,
            message = "No '${selector.description}' containing text '$text' after ${timeout}ms",
        )
        throw AssertionError("No '${selector.description}' found containing text '$text' after ${timeout}ms")
    }

    fun mozVerifyAnyHasChildWithText(
        selector: Selector,
        text: String,
        timeout: Long = TestAssetHelper.waitingTime,
        interval: Long = 500,
    ): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("verify_any_has_child_text", selector.description),
            "Verifying any '${selector.description}' has child with text '$text'...",
            1,
        )
        val deadline = System.currentTimeMillis() + timeout
        while (System.currentTimeMillis() < deadline) {
            val match =
                mozGetAllElements(selector)?.filter(hasAnyChild(hasText(text)))?.fetchSemanticsNodes()?.isNotEmpty() ==
                    true
            if (match) {
                rep?.endCmd(success = true, message = "Found '${selector.description}' with child text '$text'")
                return this
            }
            SystemClock.sleep(interval)
        }
        rep?.endCmd(
            success = false,
            message = "No '${selector.description}' with child text '$text' after ${timeout}ms",
        )
        throw AssertionError(
            "No '${selector.description}' found with a child containing text '$text' after ${timeout}ms"
        )
    }

    fun mozVerifyNoneContainText(selector: Selector, text: String): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("verify_none_contain_text", selector.description),
            "Verifying no '${selector.description}' contains text '$text'...",
            1,
        )
        dismissSoftKeyboard()
        val result = mozGetAllElements(selector)
        if (result == null) {
            rep?.endCmd(success = false, message = "Selector strategy '${selector.strategy}' not supported")
            throw AssertionError("Selector strategy '${selector.strategy}' not supported by mozVerifyNoneContainText")
        }
        result.assertAll(hasText(text).not())
        rep?.endCmd(success = true, message = "No '${selector.description}' contains text '$text'")
        return this
    }

    // Polls until the match count settles on [count] rather than sampling once: lists that are still being
    // populated pass through the expected count on their way to a larger one.
    fun mozVerifyElementCount(
        selector: Selector,
        count: Int,
        timeout: Long = TestAssetHelper.waitingTime,
        interval: Long = 500,
    ): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("verify_element_count", selector.description),
            "Verifying there are exactly $count '${selector.description}' elements...",
            1,
        )
        closeSoftKeyboard()
        val deadline = System.currentTimeMillis() + timeout
        var actual = -1
        while (System.currentTimeMillis() < deadline) {
            val elements = mozGetAllElements(selector)
            if (elements == null) {
                rep?.endCmd(success = false, message = "Selector strategy '${selector.strategy}' not supported")
                throw AssertionError("Selector strategy '${selector.strategy}' not supported by mozVerifyElementCount")
            }
            actual = elements.fetchSemanticsNodes().size
            if (actual == count) {
                rep?.endCmd(success = true, message = "Found exactly $count '${selector.description}'")
                return this
            }
            SystemClock.sleep(interval)
        }
        rep?.endCmd(
            success = false,
            message = "Expected $count '${selector.description}' but found $actual after ${timeout}ms",
        )
        ScreenDump.dump(composeRule, "mozVerifyElementCount failed: ${selector.description}")
        throw AssertionError(
            "Expected exactly $count '${selector.description}' elements but found $actual after ${timeout}ms"
        )
    }

    // ------------------------------------------------------------
    // Interaction helpers (CMD + LOC)
    // ------------------------------------------------------------

    fun mozClick(selector: Selector): BasePage {
        val rep = rep()
        rep?.startCmd(safeId("click", selector.description), "Attempting to click '${selector.description}'...", 1)
        rep?.startLoc(safeId("loc", selector.description), "Attempting to locate '${selector.description}'...", 2)

        composeRule.waitForIdle()
        var element = resolve(selector)
        if (element == null && dismissKnownOverlaysIfPresent()) {
            // A known blocking overlay (e.g. the stylus prompt) was covering the target — retry once.
            composeRule.waitForIdle()
            element = resolve(selector)
        }
        if (element == null) {
            rep?.endLoc(success = false, message = notFound(selector.description))
            rep?.endCmd(success = false, message = "Click '${selector.description}' failed: element not found")
            ScreenDump.dump(composeRule, "mozClick target not found: ${selector.description}")
            throw AssertionError(
                "Element not found for selector: ${selector.description} (${selector.strategy} -> ${selector.value})"
            )
        }
        rep?.endLoc(success = true, message = found(selector.description))

        try {
            element.click()
            rep?.endCmd(success = true, message = "Clicked '${selector.description}'")
            return this
        } catch (e: Throwable) {
            rep?.endCmd(
                success = false,
                message = "Click '${selector.description}' failed: ${e.message ?: "exception"}",
            )
            ScreenDump.dump(composeRule, "mozClick failed: ${selector.description}")
            throw e
        }
    }

    // --- Resolution facade (pilot: used by mozClick; other verbs migrate onto this over time) -------
    //
    // resolve() is the single seam between "locate" and "interact": it fetches the candidate node(s)
    // for a selector and applies ONE consistent selection policy — prefer the *displayed* match, else
    // the first — then returns a backend-agnostic UiElement. Verbs just call element.click()/etc. and
    // never switch on the underlying Compose/Espresso/UiAutomator node type. This replaces the
    // one-off, text-only mozClickDisplayed with a general rule that works for tag and text selectors
    // (and is easy to extend to more strategies).
    private fun resolve(selector: Selector, applyPreconditions: Boolean = true): UiElement? {
        if (selector.value.isBlank()) return null
        if (applyPreconditions && requiresScroll(selector.groups)) {
            ensureReachable(selector)
        }
        // Compose: resolve tag/text/content-description to the displayed match, trying BOTH the
        // merged and unmerged semantics trees so callers never have to know which one an element
        // lives in. (The merged/unmerged mismatch on COMPOSE_BY_TEXT is what broke navigation after
        // the resolve() pilot; content-description had the same latent trap.)
        resolveComposeNode(selector)?.let {
            return ComposeUiElement(it)
        }
        // Everything else (Espresso / UiAutomator, plus exotic compose strategies): reuse the existing
        // single-node fetch (preconditions already applied above) and wrap it in the facade.
        return toUiElement(mozGetElement(selector, applyPreconditions = false))
    }

    /** Return the first *displayed* node in the collection, else the first node, else null. */
    private fun pickDisplayed(collection: SemanticsNodeInteractionCollection): SemanticsNodeInteraction? {
        val count =
            try {
                collection.fetchSemanticsNodes().size
            } catch (_: Throwable) {
                0
            }
        if (count == 0) return null
        for (i in 0 until count) {
            val node = collection[i]
            try {
                node.assertIsDisplayed()
                return node
            } catch (_: AssertionError) {
                // not the on-screen match; keep looking
            }
        }
        return collection[0]
    }

    /**
     * Resolve a Compose selector (tag / text / content-description) to the displayed match, hiding the
     * merged-vs-unmerged tree distinction from callers. Tries the strategy's historical primary tree first (text ->
     * unmerged; tag/content-description -> merged), then the OTHER tree as a fallback, so a caller just supplies a
     * testTag/text/description and the facade finds it wherever it lives. Returns null for non-Compose strategies
     * (handled via mozGetElement).
     */
    private fun resolveComposeNode(selector: Selector): SemanticsNodeInteraction? {
        fun candidates(unmerged: Boolean): SemanticsNodeInteractionCollection? =
            when (selector.strategy) {
                SelectorStrategy.COMPOSE_BY_TAG ->
                    composeRule.onAllNodesWithTag(selector.value, useUnmergedTree = unmerged)
                // Tag AND the node's own text. For elements whose text also renders elsewhere on screen
                // (e.g. a host shown both in a panel and in the address bar), the tag disambiguates while
                // the text still asserts the content — neither alone is sufficient.
                SelectorStrategy.COMPOSE_BY_TAG_AND_TEXT ->
                    composeRule.onAllNodes(
                        hasTestTag(selector.value) and hasText(selector.secondaryValue ?: ""),
                        useUnmergedTree = unmerged,
                    )
                SelectorStrategy.COMPOSE_BY_TEXT,
                SelectorStrategy.COMPOSE_BY_TEXT_MERGED ->
                    composeRule.onAllNodesWithText(selector.value, useUnmergedTree = unmerged)
                SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION ->
                    composeRule.onAllNodesWithContentDescription(selector.value, useUnmergedTree = unmerged)
                else -> null
            }
        // COMPOSE_BY_TEXT historically resolved on the unmerged tree; tag/content-description on the
        // merged tree. Try each strategy's proven primary tree first (no behavior change), then the
        // other tree only if the primary yields nothing.
        val primaryUnmerged = selector.strategy == SelectorStrategy.COMPOSE_BY_TEXT
        return candidates(primaryUnmerged)?.let { pickDisplayed(it) }
            ?: candidates(!primaryUnmerged)?.let { pickDisplayed(it) }
    }

    /** Wrap a raw located node (from mozGetElement) into the backend-agnostic UiElement facade. */
    private fun toUiElement(any: Any?): UiElement? =
        when (any) {
            is SemanticsNodeInteraction -> ComposeUiElement(any)
            is ViewInteraction -> EspressoUiElement(any)
            is UiObject -> UiObjectUiElement(any)
            is UiObject2 -> UiObject2UiElement(any)
            else -> null
        }

    fun mozLongClick(selector: Selector): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("long_click", selector.description),
            "Attempting to long click '${selector.description}'...",
            1,
        )
        rep?.startLoc(safeId("loc", selector.description), "Attempting to locate '${selector.description}'...", 2)

        val element = mozGetElement(selector)
        if (element == null) {
            rep?.endLoc(success = false, message = notFound(selector.description))
            rep?.endCmd(success = false, message = "Long click '${selector.description}' failed: element not found")
            throw AssertionError(
                "Element not found for selector: ${selector.description} (${selector.strategy} -> ${selector.value})"
            )
        } else {
            rep?.endLoc(success = true, message = found(selector.description))
        }

        try {
            when (element) {
                is ViewInteraction -> element.perform(longClick())
                is UiObject -> {
                    if (!element.exists())
                        throw AssertionError("UiObject does not exist for selector: ${selector.description}")
                    if (!element.longClick())
                        throw AssertionError("Failed to long click UiObject for selector: ${selector.description}")
                }
                is UiObject2 -> element.longClick()
                is SemanticsNodeInteraction -> {
                    if (selector.strategy == SelectorStrategy.COMPOSE_BY_TEXT_MERGED) {
                        composeRule.waitUntil(TestAssetHelper.waitingTime) {
                            composeRule.onAllNodesWithText(selector.value).fetchSemanticsNodes().isNotEmpty()
                        }
                        composeRule.onNodeWithText(selector.value).performTouchInput {
                            composeLongClick(durationMillis = 5000)
                        }
                    } else {
                        element.assertExists()
                        element.assertIsDisplayed()
                        element.performTouchInput { composeLongClick(durationMillis = 5000) }
                    }
                }
                else ->
                    throw AssertionError(
                        "Unsupported element type (${element::class.simpleName}) for selector: ${selector.description}"
                    )
            }

            rep?.endCmd(success = true, message = "Long clicked '${selector.description}'")
            return this
        } catch (e: Throwable) {
            rep?.endCmd(
                success = false,
                message = "Long click '${selector.description}' failed: ${e.message ?: "exception"}",
            )
            throw e
        }
    }

    /**
     * Waits up to [timeout] ms for [selector] to appear, then clicks it if visible; silently skips if it never appears.
     *
     * Use this exclusively for UI that is genuinely optional by design (e.g. a one-time dialog that only appears on the
     * first run). Never use it as a workaround for flaky selectors or timing issues — those should be fixed at the
     * source.
     */
    fun mozClickFirstWithParentText(selector: Selector, parentText: String): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("click_first_with_parent_text", selector.description),
            "Attempting to click first '${selector.description}' with parent text '$parentText'...",
            1,
        )
        val result = mozGetAllElements(selector)
        if (result == null) {
            rep?.endCmd(success = false, message = "Selector strategy '${selector.strategy}' not supported")
            throw AssertionError(
                "Selector strategy '${selector.strategy}' not supported by mozClickFirstWithParentText"
            )
        }
        try {
            result.filter(hasParent(hasText(parentText))).onFirst().performClick()
            rep?.endCmd(
                success = true,
                message = "Clicked first '${selector.description}' with parent text '$parentText'",
            )
        } catch (e: Throwable) {
            rep?.endCmd(
                success = false,
                message =
                    "Click failed for '${selector.description}' with parent text '$parentText': ${e.message ?: "exception"}",
            )
            throw e
        }
        return this
    }

    fun mozClickIfPresent(selector: Selector, timeout: Long = 3_000, interval: Long = 200): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("click_if_present", selector.description),
            "Attempting to click '${selector.description}' if present...",
            1,
        )

        val deadline = System.currentTimeMillis() + timeout
        var present = false
        while (System.currentTimeMillis() < deadline) {
            rep?.startLoc(safeId("loc", selector.description), "Attempting to locate '${selector.description}'...", 2)
            present = mozVerifyElement(selector, applyPreconditions = false)
            rep?.endLoc(
                success = present,
                message = if (present) found(selector.description) else notFound(selector.description),
            )
            if (present) break
            SystemClock.sleep(interval)
        }

        if (!present) {
            rep?.endCmdSkip(message = "'${selector.description}' not present after ${timeout}ms")
            return this
        }

        val element =
            mozGetElement(selector)
                ?: run {
                    rep?.endCmdSkip(message = "'${selector.description}' vanished before click")
                    return this
                }

        try {
            when (element) {
                is ViewInteraction -> element.perform(click())
                is UiObject -> {
                    if (element.exists()) element.click()
                }
                is UiObject2 -> element.click()
                is SemanticsNodeInteraction -> {
                    element.assertExists()
                    element.assertIsDisplayed()
                    element.performClick()
                }
                else ->
                    throw AssertionError(
                        "Unsupported element type (${element::class.simpleName}) for selector: ${selector.description}"
                    )
            }

            rep?.endCmd(success = true, message = "Clicked '${selector.description}'")
            return this
        } catch (e: Throwable) {
            rep?.endCmd(
                success = false,
                message = "Click '${selector.description}' failed: ${e.message ?: "exception"}",
            )
            throw e
        }
    }

    /**
     * Polls until [selector] is present AND enabled, then clicks it. Use for a control that renders immediately but is
     * briefly disabled (e.g. the add-on permission dialog's "Add" button, which PermissionsDialogFragment disables for
     * ~1s): [mozClick]/[mozClickIfPresent] check presence only and would tap the still-disabled control, which the app
     * ignores — a silent no-op. Dispatches across all element backends so it works regardless of the selector's
     * strategy.
     */
    fun mozClickWhenEnabled(
        selector: Selector,
        timeout: Long = TestAssetHelper.waitingTime,
        interval: Long = 200,
    ): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("click_when_enabled", selector.description),
            "Attempting to click '${selector.description}' once enabled...",
            1,
        )

        val deadline = System.currentTimeMillis() + timeout
        var element: Any? = null
        while (System.currentTimeMillis() < deadline) {
            rep?.startLoc(
                safeId("loc", selector.description),
                "Waiting for '${selector.description}' to be enabled...",
                2,
            )
            element = mozGetElement(selector, applyPreconditions = false)
            val enabled = element != null && isElementEnabled(element)
            rep?.endLoc(
                success = enabled,
                message = if (enabled) found(selector.description) else notFound(selector.description),
            )
            if (enabled) break
            element = null
            SystemClock.sleep(interval)
        }

        if (element == null) {
            rep?.endCmd(success = false, message = "'${selector.description}' not enabled after ${timeout}ms")
            ScreenDump.dump(composeRule, "mozClickWhenEnabled: '${selector.description}' never became enabled")
            throw AssertionError(
                "'${selector.description}' was expected to become enabled but did not, after ${timeout}ms"
            )
        }

        try {
            when (element) {
                is ViewInteraction -> element.perform(click())
                is UiObject -> element.click()
                is UiObject2 -> element.click()
                is SemanticsNodeInteraction -> {
                    element.assertExists()
                    element.assertIsDisplayed()
                    element.performClick()
                }
                else ->
                    throw AssertionError(
                        "Unsupported element type (${element::class.simpleName}) for selector: ${selector.description}"
                    )
            }
            rep?.endCmd(success = true, message = "Clicked '${selector.description}'")
            return this
        } catch (e: Throwable) {
            rep?.endCmd(
                success = false,
                message = "Click '${selector.description}' failed: ${e.message ?: "exception"}",
            )
            ScreenDump.dump(composeRule, "mozClickWhenEnabled failed: ${selector.description}")
            throw e
        }
    }

    /** Exception-safe "is this element enabled right now?" probe, dispatched across all backends. */
    private fun isElementEnabled(element: Any?): Boolean =
        try {
            when (element) {
                is ViewInteraction -> {
                    element.check(matches(isEnabled()))
                    true
                }
                is UiObject -> element.isEnabled
                is UiObject2 -> element.isEnabled
                is SemanticsNodeInteraction -> {
                    element.assertExists()
                    element.assertIsEnabled()
                    true
                }
                else -> false
            }
        } catch (_: Throwable) {
            false
        }

    /**
     * Presses back until [selector] disappears, bounded by [maxPresses]. Mirrors the legacy exitMenu() pattern: gating
     * on the anchor's disappearance rather than a fixed back-press count tolerates presses that are swallowed while a
     * Compose/fragment transition is still settling (waitForIdle() can return early), which is the cause of
     * intermittent "Failed to navigate to HomePage" failures when backing out of nested Settings.
     */
    fun mozPressBackUntilGone(selector: Selector, maxPresses: Int = 5): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("press_back_until_gone", selector.description),
            "Pressing back until '${selector.description}' is gone...",
            1,
        )

        repeat(maxPresses) { attempt ->
            rep?.startLoc(
                safeId("loc", "${selector.description}_attempt_${attempt + 1}"),
                "Attempting to locate '${selector.description}'...",
                2,
            )
            val present = waitForPresence(selector, TestAssetHelper.waitingTimeShort)
            rep?.endLoc(
                success = !present,
                message = if (present) found(selector.description) else notFound(selector.description),
            )

            if (!present) {
                rep?.endCmd(success = true, message = "'${selector.description}' gone after $attempt back press(es)")
                return this
            }

            mDevice.pressBack()
            mDevice.waitForIdle()
        }

        rep?.endCmd(
            success = false,
            message = "'${selector.description}' still present after $maxPresses back press(es)",
        )
        throw AssertionError("'${selector.description}' still present after $maxPresses back press(es)")
    }

    // A single back press. mozPressBackUntilGone cannot stand in when the thing being left has no selector
    // to poll — closing a 404 tab to return to the previous one, for instance — and "back until X is gone"
    // would press again if the first press has not landed yet.
    fun mozPressBack(): BasePage {
        val rep = rep()
        rep?.startCmd("press_back", "Pressing back...", 1)
        mDevice.pressBack()
        mDevice.waitForIdle()
        rep?.endCmd(success = true, message = "Pressed back")
        return this
    }

    /**
     * Press back until [selector] is showing, up to [maxPresses] times.
     *
     * The counterpart to [mozPressBackUntilGone], for backing out of an unknown number of screens: how deep you are can
     * depend on whether a dialog or a fragment intercepted an earlier step, and a fixed number of presses either
     * overshoots (backgrounding the app) or stops short.
     */
    fun mozPressBackUntilPresent(selector: Selector, maxPresses: Int = 5): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("press_back_until_present", selector.description),
            "Pressing back until '${selector.description}' is showing...",
            1,
        )

        repeat(maxPresses) { attempt ->
            if (mozVerifyElement(selector, applyPreconditions = false)) {
                rep?.endCmd(success = true, message = "'${selector.description}' showing after $attempt back press(es)")
                return this
            }
            mDevice.pressBack()
            mDevice.waitForIdle()
        }

        if (mozVerifyElement(selector, applyPreconditions = false)) {
            rep?.endCmd(success = true, message = "'${selector.description}' showing after $maxPresses back press(es)")
            return this
        }
        rep?.endCmd(
            success = false,
            message = "'${selector.description}' never appeared after $maxPresses back press(es)",
        )
        ScreenDump.dump(composeRule, "mozPressBackUntilPresent failed: ${selector.description}")
        throw AssertionError("'${selector.description}' did not appear after $maxPresses back press(es)")
    }

    private fun waitForPresence(selector: Selector, timeout: Long, interval: Long = 200): Boolean {
        val deadline = System.currentTimeMillis() + timeout
        while (System.currentTimeMillis() < deadline) {
            if (mozVerifyElement(selector, applyPreconditions = false)) return true
            android.os.SystemClock.sleep(interval)
        }
        return false
    }

    fun mozSwipeTo(
        selector: Selector,
        direction: SwipeDirection = SwipeDirection.DOWN,
        maxSwipes: Int = 10, // TODO (Jackie J. 10/30/2025): replace hard-coded value with self-selecting x,y boundaries
        applyPreconditions: Boolean = false, // default false to avoid recursive preconditions
    ): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("swipe_to", selector.description),
            "Attempting to swipe to '${selector.description}'...",
            1,
        )

        try {
            repeat(maxSwipes) { attempt ->
                // Each attempt is a LOC check for visibility.
                rep?.startLoc(
                    safeId("loc", "${selector.description}_attempt_${attempt + 1}"),
                    "Attempting to locate '${selector.description}'...",
                    2,
                )
                val element = mozGetElement(selector, applyPreconditions = applyPreconditions)

                val isVisible =
                    when (element) {
                        // Espresso's click() rejects views displayed under CLICKABLE_VISIBILITY_PERCENT,
                        // so stop swiping only once the element clears that bar.
                        is ViewInteraction ->
                            try {
                                element.check(matches(isDisplayingAtLeast(CLICKABLE_VISIBILITY_PERCENT)))
                                true
                            } catch (_: Throwable) {
                                false
                            }
                        is UiObject -> element.exists()
                        is UiObject2 -> true
                        is SemanticsNodeInteraction ->
                            try {
                                element.assertExists()
                                element.assertIsDisplayed()
                                true
                            } catch (_: AssertionError) {
                                false
                            }
                        else -> false
                    }

                rep?.endLoc(
                    success = isVisible,
                    message = if (isVisible) found(selector.description) else notFound(selector.description),
                )

                if (isVisible) {
                    Log.i("MozSwipeTo", "Element '${selector.description}' found after $attempt swipe(s)")
                    rep?.endCmd(
                        success = true,
                        message = "Reached '${selector.description}' after ${attempt + 1} swipe(s)",
                    )
                    return this
                }

                // The swipe itself is an action; we keep it as part of the CMD.
                performSwipe(direction)
                Thread.sleep(500)
            }

            rep?.endCmd(
                success = false,
                message = "Swipe-to '${selector.description}' failed after $maxSwipes attempts",
            )
            throw AssertionError("Element '${selector.description}' not found after $maxSwipes swipe(s)")
        } catch (t: Throwable) {
            rep?.endCmd(
                success = false,
                message = "Swipe-to '${selector.description}' failed: ${t.message ?: "exception"}",
            )
            throw t
        }
    }

    /**
     * Swipe a single [direction] on [selector]'s element. [steps] controls the gesture speed for the UiAutomator
     * ([UiObject]) backend: it is the number of motion events sent, so a low value produces a fast flick and a high
     * value a slow drag. Some gestures only register as a flick (e.g. swiping the navigation toolbar to switch tabs),
     * so callers that need one pass a small [steps]; the default of 100 keeps the original slow-drag behaviour for
     * existing callers.
     */
    fun mozSwipeElement(
        selector: Selector,
        direction: SwipeDirection,
        applyPreconditions: Boolean = false,
        steps: Int = 100,
    ): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("swipe_element", selector.description),
            "Swiping ${direction.name} on '${selector.description}'...",
            1,
        )

        try {
            val containerElement = mozGetElement(selector, applyPreconditions = applyPreconditions)

            when (containerElement) {
                is ViewInteraction -> {
                    val action =
                        when (direction) {
                            SwipeDirection.DOWN -> swipeDown()
                            SwipeDirection.UP -> swipeUp()
                            SwipeDirection.RIGHT -> swipeRight()
                            SwipeDirection.LEFT -> swipeLeft()
                        }
                    containerElement.perform(action)
                }

                is UiObject -> {
                    when (direction) {
                        SwipeDirection.DOWN -> containerElement.swipeDown(steps)
                        SwipeDirection.UP -> containerElement.swipeUp(steps)
                        SwipeDirection.RIGHT -> containerElement.swipeRight(steps)
                        SwipeDirection.LEFT -> containerElement.swipeLeft(steps)
                    }
                }

                is UiObject2 -> {
                    val swipePercent = 1.0f

                    val uiAutomatorDirection =
                        when (direction) {
                            SwipeDirection.DOWN -> androidx.test.uiautomator.Direction.DOWN
                            SwipeDirection.UP -> androidx.test.uiautomator.Direction.UP
                            SwipeDirection.RIGHT -> androidx.test.uiautomator.Direction.RIGHT
                            SwipeDirection.LEFT -> androidx.test.uiautomator.Direction.LEFT
                        }

                    containerElement.swipe(uiAutomatorDirection, swipePercent)
                }

                is SemanticsNodeInteraction -> {
                    containerElement.performTouchInput {
                        val swipeDistance = 1500f
                        val swipeDuration = 200L

                        when (direction) {
                            SwipeDirection.DOWN ->
                                swipe(
                                    start = center,
                                    end = androidx.compose.ui.geometry.Offset(center.x, center.y + swipeDistance),
                                    durationMillis = swipeDuration,
                                )
                            SwipeDirection.UP ->
                                swipe(
                                    start = center,
                                    end = androidx.compose.ui.geometry.Offset(center.x, center.y - swipeDistance),
                                    durationMillis = swipeDuration,
                                )
                            SwipeDirection.RIGHT ->
                                swipe(
                                    start = center,
                                    end = androidx.compose.ui.geometry.Offset(center.x + swipeDistance, center.y),
                                    durationMillis = swipeDuration,
                                )
                            SwipeDirection.LEFT ->
                                swipe(
                                    start = center,
                                    end = androidx.compose.ui.geometry.Offset(center.x - swipeDistance, center.y),
                                    durationMillis = swipeDuration,
                                )
                        }
                    }
                }

                else -> {
                    throw IllegalArgumentException(
                        "Unsupported element type for targeted swiping: ${containerElement?.javaClass?.simpleName}"
                    )
                }
            }

            rep?.endCmd(success = true, message = "Successfully swiped ${direction.name} on '${selector.description}'")
        } catch (t: Throwable) {
            rep?.endCmd(
                success = false,
                message = "Failed to swipe on '${selector.description}': ${t.message ?: "exception"}",
            )
            throw t
        }

        return this
    }

    fun mozSwipeElementUntilAbsent(
        selector: Selector,
        direction: SwipeDirection,
        maxSwipes: Int = 3,
        applyPreconditions: Boolean = false,
    ): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("swipe_element_until_absent", selector.description),
            "Swiping ${direction.name} on '${selector.description}' until absent...",
            1,
        )

        repeat(maxSwipes) { attempt ->
            rep?.startLoc(
                safeId("loc", "${selector.description}_attempt_${attempt + 1}"),
                "Attempting to locate '${selector.description}'...",
                2,
            )
            val present = mozVerifyElement(selector, applyPreconditions = false)
            rep?.endLoc(
                success = !present,
                message = if (present) found(selector.description) else notFound(selector.description),
            )

            if (!present) {
                rep?.endCmd(success = true, message = "'${selector.description}' gone after $attempt swipe(s)")
                return this
            }

            mozSwipeElement(selector, direction, applyPreconditions)
            composeRule.waitForIdle()
            mDevice.waitForIdle()
        }

        rep?.endCmd(success = false, message = "'${selector.description}' still present after $maxSwipes swipe(s)")
        ScreenDump.dump(composeRule, "mozSwipeElementUntilAbsent failed: ${selector.description}")
        throw AssertionError("'${selector.description}' still present after $maxSwipes swipe(s)")
    }

    fun mozOpenNotificationsTray(): BasePage {
        val rep = rep()
        rep?.startCmd("open_notifications_tray", "Attempting to open Notifications tray...", 1)
        return try {
            mDevice.openNotification()
            rep?.endCmd(success = true, message = "Notifications tray opened")
            this
        } catch (t: Throwable) {
            rep?.endCmd(success = false, message = "Open Notifications tray failed: ${t.message ?: "exception"}")
            throw t
        }
    }

    private fun performSwipe(direction: SwipeDirection) {
        val rep = rep()
        rep?.startCmd(safeId("swipe", direction.name), "Attempting to swipe ${direction.name.lowercase()}...", 2)

        try {
            val height = mDevice.displayHeight
            val width = mDevice.displayWidth

            val (startX, startY, endX, endY) =
                when (direction) {
                    SwipeDirection.UP -> listOf(width / 2, height / 2, width / 2, height / 4)
                    SwipeDirection.DOWN -> listOf(width / 2, height / 2, width / 2, height * 3 / 4)
                    SwipeDirection.LEFT -> listOf(width * 3 / 4, height / 2, width / 4, height / 2)
                    SwipeDirection.RIGHT -> listOf(width / 4, height / 2, width * 3 / 4, height / 2)
                }

            mDevice.swipe(startX, startY, endX, endY, 20)
            rep?.endCmd(success = true, message = "Swipe ${direction.name.lowercase()} completed")
        } catch (t: Throwable) {
            rep?.endCmd(
                success = false,
                message = "Swipe ${direction.name.lowercase()} failed: ${t.message ?: "exception"}",
            )
            throw t
        }
    }

    fun mozEnterText(text: String, selector: Selector): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("enter_text", selector.description),
            "Attempting to enter text into '${selector.description}'...",
            1,
        )
        rep?.startLoc(safeId("loc", selector.description), "Attempting to locate '${selector.description}'...", 2)

        val element = mozGetElement(selector)
        if (element == null) {
            rep?.endLoc(success = false, message = notFound(selector.description))
            rep?.endCmd(success = false, message = "Enter text failed: element not found ('${selector.description}')")
            throw AssertionError(
                "Element not found for selector: ${selector.description} (${selector.strategy} -> ${selector.value})"
            )
        } else {
            rep?.endLoc(success = true, message = found(selector.description))
        }

        try {
            when (element) {
                is ViewInteraction -> element.perform(typeText(text))
                is UiObject -> element.setText(text)
                is UiObject2 -> element.setText(text)
                is SemanticsNodeInteraction -> element.performTextInput(text)
                else ->
                    throw AssertionError(
                        "Unsupported element type (${element::class.simpleName}) for selector: ${selector.description}"
                    )
            }

            rep?.endCmd(success = true, message = "Entered text into '${selector.description}'")
            return this
        } catch (e: Throwable) {
            rep?.endCmd(
                success = false,
                message = "Enter text failed for '${selector.description}': ${e.message ?: "exception"}",
            )
            throw AssertionError("Failed to enter text for selector: ${selector.description}", e)
        }
    }

    fun mozPressEnter(selector: Selector): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("press_enter", selector.description),
            "Attempting to press Enter on '${selector.description}'...",
            1,
        )
        rep?.startLoc(safeId("loc", selector.description), "Attempting to locate '${selector.description}'...", 2)

        val element = mozGetElement(selector)
        if (element == null) {
            rep?.endLoc(success = false, message = notFound(selector.description))
            rep?.endCmd(success = false, message = "Press Enter failed: element not found ('${selector.description}')")
            throw AssertionError(
                "Element not found for selector: ${selector.description} (${selector.strategy} -> ${selector.value})"
            )
        } else {
            rep?.endLoc(success = true, message = found(selector.description))
        }

        try {
            when (element) {
                is ViewInteraction -> element.perform(pressImeActionButton())
                is UiObject -> mDevice.pressEnter()
                is UiObject2 -> mDevice.pressEnter()
                is SemanticsNodeInteraction -> element.performImeAction()
                else ->
                    throw AssertionError(
                        "Unsupported element type (${element::class.simpleName}) for selector: ${selector.description}"
                    )
            }

            rep?.endCmd(success = true, message = "Pressed Enter on '${selector.description}'")
            return this
        } catch (e: Throwable) {
            rep?.endCmd(
                success = false,
                message = "Press Enter failed for '${selector.description}': ${e.message ?: "exception"}",
            )
            throw AssertionError("Failed to press Enter for selector: ${selector.description}", e)
        }
    }

    /**
     * Drive a Compose slider to [value] via its SetProgress semantics action, rather than a touch drag. A synthetic
     * swipe can only land on whatever step the gesture geometry happens to hit; SetProgress asks the slider for an
     * exact value, which is what the legacy accessibility test relied on to set a precise font-size percentage.
     * Compose-tag selectors only.
     */
    fun mozSetSliderValue(selector: Selector, value: Float): BasePage {
        val rep = rep()
        rep?.startCmd(safeId("set_slider", selector.description), "Setting '${selector.description}' to $value...", 1)
        try {
            val node = composeRule.onNodeWithTag(selector.value)
            node.assertExists()
            node.performSemanticsAction(SemanticsActions.SetProgress) { it(value) }
            rep?.endCmd(success = true, message = "Set '${selector.description}' to $value")
            return this
        } catch (e: Throwable) {
            rep?.endCmd(
                success = false,
                message = "Set slider '${selector.description}' failed: ${e.message ?: "exception"}",
            )
            ScreenDump.dump(composeRule, "mozSetSliderValue failed: ${selector.description}")
            throw e
        }
    }

    fun mozVerifyElementIsSelected(selector: Selector, applyPreconditions: Boolean = true): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("verify_selected", selector.description),
            "Verifying '${selector.description}' is selected...",
            1,
        )
        rep?.startLoc(safeId("loc", selector.description), "Attempting to locate '${selector.description}'...", 2)

        val element = mozGetElement(selector, applyPreconditions = applyPreconditions)
        if (element == null) {
            rep?.endLoc(success = false, message = notFound(selector.description))
            rep?.endCmd(success = false, message = "'${selector.description}' not found")
            throw AssertionError(
                "Element not found for selector: ${selector.description} (${selector.strategy} -> ${selector.value})"
            )
        }
        rep?.endLoc(success = true, message = found(selector.description))

        try {
            when (element) {
                is ViewInteraction -> element.check(matches(isSelected()))
                is UiObject ->
                    if (!element.isSelected) throw AssertionError("'${selector.description}' is not selected")
                is UiObject2 ->
                    if (!element.isSelected) throw AssertionError("'${selector.description}' is not selected")
                is SemanticsNodeInteraction -> {
                    element.assertExists()
                    element.assertIsSelected()
                }
                else ->
                    throw AssertionError(
                        "Unsupported element type (${element::class.simpleName}) for selector: ${selector.description}"
                    )
            }
        } catch (e: Throwable) {
            rep?.endCmd(success = false, message = "'${selector.description}' is not selected")
            throw e
        }

        rep?.endCmd(success = true, message = "'${selector.description}' is selected")
        return this
    }

    fun mozVerifyElementIsNotSelected(selector: Selector, applyPreconditions: Boolean = true): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("verify_not_selected", selector.description),
            "Verifying '${selector.description}' is not selected...",
            1,
        )
        rep?.startLoc(safeId("loc", selector.description), "Attempting to locate '${selector.description}'...", 2)

        val element = mozGetElement(selector, applyPreconditions = applyPreconditions)
        if (element == null) {
            rep?.endLoc(success = false, message = notFound(selector.description))
            rep?.endCmd(success = false, message = "'${selector.description}' not found")
            throw AssertionError(
                "Element not found for selector: ${selector.description} (${selector.strategy} -> ${selector.value})"
            )
        }
        rep?.endLoc(success = true, message = found(selector.description))

        try {
            when (element) {
                is ViewInteraction -> element.check(matches(isNotSelected()))
                is UiObject -> if (element.isSelected) throw AssertionError("'${selector.description}' is selected")
                is UiObject2 -> if (element.isSelected) throw AssertionError("'${selector.description}' is selected")
                is SemanticsNodeInteraction -> {
                    element.assertExists()
                    element.assertIsNotSelected()
                }
                else ->
                    throw AssertionError(
                        "Unsupported element type (${element::class.simpleName}) for selector: ${selector.description}"
                    )
            }
        } catch (e: Throwable) {
            rep?.endCmd(success = false, message = "'${selector.description}' is selected")
            throw e
        }

        rep?.endCmd(success = true, message = "'${selector.description}' is not selected")
        return this
    }

    fun mozVerifyElementIsEnabled(selector: Selector, applyPreconditions: Boolean = true): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("verify_enabled", selector.description),
            "Verifying '${selector.description}' is enabled...",
            1,
        )
        rep?.startLoc(safeId("loc", selector.description), "Attempting to locate '${selector.description}'...", 2)

        val element = mozGetElement(selector, applyPreconditions = applyPreconditions)
        if (element == null) {
            rep?.endLoc(success = false, message = notFound(selector.description))
            rep?.endCmd(success = false, message = "'${selector.description}' not found")
            throw AssertionError(
                "Element not found for selector: ${selector.description} (${selector.strategy} -> ${selector.value})"
            )
        }
        rep?.endLoc(success = true, message = found(selector.description))

        try {
            when (element) {
                is ViewInteraction -> element.check(matches(isEnabled()))
                is UiObject -> if (!element.isEnabled) throw AssertionError("'${selector.description}' is not enabled")
                is UiObject2 -> if (!element.isEnabled) throw AssertionError("'${selector.description}' is not enabled")
                is SemanticsNodeInteraction -> {
                    element.assertExists()
                    element.assertIsEnabled()
                }
                else ->
                    throw AssertionError(
                        "Unsupported element type (${element::class.simpleName}) for selector: ${selector.description}"
                    )
            }
        } catch (e: Throwable) {
            rep?.endCmd(success = false, message = "'${selector.description}' is not enabled")
            throw e
        }

        rep?.endCmd(success = true, message = "'${selector.description}' is enabled")
        return this
    }

    fun mozVerifyElementIsChecked(selector: Selector, applyPreconditions: Boolean = true): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("verify_checked", selector.description),
            "Verifying '${selector.description}' is checked...",
            1,
        )
        rep?.startLoc(safeId("loc", selector.description), "Attempting to locate '${selector.description}'...", 2)

        val element = mozGetElement(selector, applyPreconditions = applyPreconditions)
        if (element == null) {
            rep?.endLoc(success = false, message = notFound(selector.description))
            rep?.endCmd(success = false, message = "'${selector.description}' not found")
            throw AssertionError(
                "Element not found for selector: ${selector.description} (${selector.strategy} -> ${selector.value})"
            )
        }
        rep?.endLoc(success = true, message = found(selector.description))

        try {
            when (element) {
                is ViewInteraction -> element.check(matches(isChecked()))
                is UiObject -> if (!element.isChecked) throw AssertionError("'${selector.description}' is not checked")
                is UiObject2 -> if (!element.isChecked) throw AssertionError("'${selector.description}' is not checked")
                else ->
                    throw AssertionError(
                        "Unsupported element type (${element::class.simpleName}) for selector: ${selector.description}"
                    )
            }
        } catch (e: Throwable) {
            rep?.endCmd(success = false, message = "'${selector.description}' is not checked")
            throw e
        }

        rep?.endCmd(success = true, message = "'${selector.description}' is checked")
        return this
    }

    fun mozVerifyElementIsNotChecked(selector: Selector, applyPreconditions: Boolean = true): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("verify_not_checked", selector.description),
            "Verifying '${selector.description}' is not checked...",
            1,
        )
        rep?.startLoc(safeId("loc", selector.description), "Attempting to locate '${selector.description}'...", 2)

        val element = mozGetElement(selector, applyPreconditions = applyPreconditions)
        if (element == null) {
            rep?.endLoc(success = false, message = notFound(selector.description))
            rep?.endCmd(success = false, message = "'${selector.description}' not found")
            throw AssertionError(
                "Element not found for selector: ${selector.description} (${selector.strategy} -> ${selector.value})"
            )
        }
        rep?.endLoc(success = true, message = found(selector.description))

        try {
            when (element) {
                is ViewInteraction -> element.check(matches(isNotChecked()))
                is UiObject -> if (element.isChecked) throw AssertionError("'${selector.description}' is checked")
                is UiObject2 -> if (element.isChecked) throw AssertionError("'${selector.description}' is checked")
                else ->
                    throw AssertionError(
                        "Unsupported element type (${element::class.simpleName}) for selector: ${selector.description}"
                    )
            }
        } catch (e: Throwable) {
            rep?.endCmd(success = false, message = "'${selector.description}' is checked")
            throw e
        }

        rep?.endCmd(success = true, message = "'${selector.description}' is not checked")
        return this
    }

    fun mozVerifyElementIsNotEnabled(selector: Selector, applyPreconditions: Boolean = true): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("verify_not_enabled", selector.description),
            "Verifying '${selector.description}' is not enabled...",
            1,
        )
        rep?.startLoc(safeId("loc", selector.description), "Attempting to locate '${selector.description}'...", 2)

        val element = mozGetElement(selector, applyPreconditions = applyPreconditions)
        if (element == null) {
            rep?.endLoc(success = false, message = notFound(selector.description))
            rep?.endCmd(success = false, message = "'${selector.description}' not found")
            throw AssertionError(
                "Element not found for selector: ${selector.description} (${selector.strategy} -> ${selector.value})"
            )
        }
        rep?.endLoc(success = true, message = found(selector.description))

        try {
            when (element) {
                is ViewInteraction -> element.check(matches(not(isEnabled())))
                is UiObject -> if (element.isEnabled) throw AssertionError("'${selector.description}' is enabled")
                is UiObject2 -> if (element.isEnabled) throw AssertionError("'${selector.description}' is enabled")
                is SemanticsNodeInteraction -> {
                    element.assertExists()
                    element.assertIsNotEnabled()
                }
                else ->
                    throw AssertionError(
                        "Unsupported element type (${element::class.simpleName}) for selector: ${selector.description}"
                    )
            }
        } catch (e: Throwable) {
            rep?.endCmd(success = false, message = "'${selector.description}' is enabled")
            throw e
        }

        rep?.endCmd(success = true, message = "'${selector.description}' is not enabled")
        return this
    }

    fun mozVerifyElementHasCheckedSiblingByResName(selector: Selector, siblingResName: String): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("verify_checked_sibling", selector.description),
            "Verifying '${selector.description}' has a checked sibling '$siblingResName'...",
            1,
        )
        rep?.startLoc(safeId("loc", selector.description), "Attempting to locate '${selector.description}'...", 2)

        val element = mozGetElement(selector)
        if (element == null) {
            rep?.endLoc(success = false, message = notFound(selector.description))
            rep?.endCmd(success = false, message = "'${selector.description}' not found")
            throw AssertionError(
                "Element not found for selector: ${selector.description} (${selector.strategy} -> ${selector.value})"
            )
        }
        rep?.endLoc(success = true, message = found(selector.description))

        when (element) {
            is ViewInteraction ->
                element.check(
                    matches(
                        hasSibling(
                            allOf(
                                withResourceName(containsString(siblingResName)),
                                isChecked(),
                            )
                        )
                    )
                )
            else ->
                throw AssertionError(
                    "mozVerifyElementHasCheckedSiblingByResName only supports Espresso selectors for: ${selector.description}"
                )
        }

        rep?.endCmd(success = true, message = "'${selector.description}' has a checked sibling '$siblingResName'")
        return this
    }

    fun mozVerifyElementHasSiblingWithText(
        selector: Selector,
        siblingText: String,
        applyPreconditions: Boolean = true,
    ): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("verify_sibling_text", selector.description),
            "Verifying '${selector.description}' has sibling with text '$siblingText'...",
            1,
        )
        rep?.startLoc(safeId("loc", selector.description), "Attempting to locate '${selector.description}'...", 2)

        val element = mozGetElement(selector, applyPreconditions = applyPreconditions)
        if (element == null) {
            rep?.endLoc(success = false, message = notFound(selector.description))
            rep?.endCmd(success = false, message = "'${selector.description}' not found")
            throw AssertionError(
                "Element not found for selector: ${selector.description} (${selector.strategy} -> ${selector.value})"
            )
        }
        rep?.endLoc(success = true, message = found(selector.description))

        try {
            when (element) {
                is ViewInteraction -> element.check(matches(hasSibling(withText(siblingText))))
                is UiObject -> {
                    val sibling = element.getFromParent(UiSelector().text(siblingText))
                    if (!sibling.exists())
                        throw AssertionError("'${selector.description}' has no sibling with text '$siblingText'")
                }
                is UiObject2 -> {
                    val sibling = element.parent?.findObject(By.text(siblingText))
                    if (sibling == null)
                        throw AssertionError("'${selector.description}' has no sibling with text '$siblingText'")
                }
                is SemanticsNodeInteraction -> element.assert(hasAnySibling(hasText(siblingText)))
                else -> throw AssertionError("Unsupported element type for selector: ${selector.description}")
            }
        } catch (e: Throwable) {
            rep?.endCmd(success = false, message = "'${selector.description}' has no sibling with text '$siblingText'")
            throw e
        }

        rep?.endCmd(success = true, message = "'${selector.description}' has sibling with text '$siblingText'")
        return this
    }

    // ------------------------------------------------------------
    // Element resolution + verification (LOC)
    // ------------------------------------------------------------

    private fun mozGetAllElements(selector: Selector): SemanticsNodeInteractionCollection? {
        if (selector.value.isBlank()) return null
        return when (selector.strategy) {
            SelectorStrategy.COMPOSE_BY_TAG,
            SelectorStrategy.COMPOSE_ON_ALL_NODES_BY_TAG_ON_FIRST -> composeRule.onAllNodesWithTag(selector.value)
            else -> null
        }
    }

    private fun mozGetElement(selector: Selector, applyPreconditions: Boolean = true): Any? {
        if (selector.value.isBlank()) {
            Log.i("mozGetElement", "Empty or blank selector value: ${selector.description}")
            return null
        }

        if (applyPreconditions && requiresScroll(selector.groups)) {
            ensureReachable(selector) // may call mozSwipeTo with applyPreconditions = false
        }

        return when (selector.strategy) {
            SelectorStrategy.COMPOSE_BY_TAG -> {
                try {
                    composeRule.onNodeWithTag(selector.value)
                } catch (_: Exception) {
                    Log.i("mozGetElement", "Compose node not found for tag: ${selector.value}")
                    null
                }
            }

            SelectorStrategy.COMPOSE_BY_TAG_AND_TEXT -> {
                val textToMatch = selector.secondaryValue ?: ""
                try {
                    // Unmerged: the tag and the text sit on the same Text node, which a merging
                    // ancestor would otherwise absorb.
                    composeRule.onNode(
                        hasTestTag(selector.value) and hasText(textToMatch),
                        useUnmergedTree = true,
                    )
                } catch (_: Exception) {
                    Log.i(
                        "mozGetElement",
                        "Compose node not found for tag: ${selector.value} with text: $textToMatch",
                    )
                    null
                }
            }
            SelectorStrategy.COMPOSE_BY_TAG_AND_CONTENT_DESCRIPTION_SUBSTRING -> {
                val descriptionToMatch = selector.secondaryValue ?: ""
                try {
                    composeRule.onNode(
                        hasTestTag(selector.value) and hasContentDescription(descriptionToMatch, substring = true)
                    )
                } catch (_: Exception) {
                    Log.i(
                        "mozGetElement",
                        "Compose node not found for tag: ${selector.value} with content description: $descriptionToMatch",
                    )
                    null
                }
            }
            // TODO: easier way to isolate parent/child/sibling elements, auto-selects sibilings or children on failure
            // as a back-up
            SelectorStrategy.COMPOSE_ON_ALL_NODES_BY_TAG_ON_FIRST -> {
                try {
                    composeRule.onAllNodesWithTag(selector.value).onFirst()
                } catch (_: Exception) {
                    Log.i("mozGetElement", "Compose node not found for tag: ${selector.value}")
                    null
                }
            }

            SelectorStrategy.COMPOSE_ON_ALL_NODES_BY_TAG_WITH_CHILD_TEXT_ON_FIRST -> {
                val textToMatch = selector.secondaryValue ?: ""
                try {
                    composeRule.onAllNodesWithTag(selector.value).filter(hasAnyChild(hasText(textToMatch))).onFirst()
                } catch (_: Exception) {
                    Log.i(
                        "mozGetElement",
                        "Compose node not found for tag: ${selector.value} with child text: $textToMatch",
                    )
                    null
                }
            }

            SelectorStrategy.COMPOSE_BY_TEXT -> {
                try {
                    composeRule.onNodeWithText(selector.value, useUnmergedTree = true)
                } catch (_: Exception) {
                    Log.i("mozGetElement", "Compose node not found for text: ${selector.value}")
                    null
                }
            }

            SelectorStrategy.COMPOSE_BY_TEXT_MERGED -> composeRule.onNodeWithText(selector.value)

            SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION -> {
                try {
                    composeRule.onNodeWithContentDescription(selector.value)
                } catch (_: Exception) {
                    Log.i("mozGetElement", "Compose node not found for content description: ${selector.value}")
                    null
                }
            }

            SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_SUBSTRING -> {
                try {
                    composeRule.onNodeWithContentDescription(selector.value, substring = true)
                } catch (_: Exception) {
                    Log.i("mozGetElement", "Compose node not found for content description: ${selector.value}")
                    null
                }
            }

            SelectorStrategy.COMPOSE_EDITABLE_BY_ANCESTOR_TAG -> {
                try {
                    composeRule.onNode(hasSetTextAction() and hasAnyAncestor(hasTestTag(selector.value)))
                } catch (_: Exception) {
                    Log.i("mozGetElement", "Editable compose node not found under tag: ${selector.value}")
                    null
                }
            }

            SelectorStrategy.ESPRESSO_BY_ID -> {
                val resId = selector.toResourceId()
                if (resId == 0) {
                    Log.i("mozGetElement", "Invalid resource ID for: ${selector.value}")
                    null
                } else {
                    onView(withId(resId))
                }
            }

            SelectorStrategy.ESPRESSO_BY_ID_WITH_SIBLING_TEXT -> {
                val resId = selector.toResourceId()
                val siblingText = selector.secondaryValue ?: ""

                if (resId == 0) {
                    Log.i("mozGetElement", "Invalid resource ID for: ${selector.value}")
                    null
                } else {
                    onView(
                        allOf(
                            withId(resId),
                            hasSibling(withText(siblingText)),
                        )
                    )
                }
            }

            SelectorStrategy.ESPRESSO_BY_TEXT -> onView(withText(selector.value))
            SelectorStrategy.ESPRESSO_BY_TEXT_WITH_SIBLING_TEXT -> {
                val siblingText = selector.secondaryValue ?: ""

                onView(
                    allOf(
                        withText(selector.value),
                        hasSibling(withText(siblingText)),
                    )
                )
            }
            SelectorStrategy.ESPRESSO_BY_CONTENT_DESC -> onView(withContentDescription(selector.value))
            SelectorStrategy.ESPRESSO_BY_RES_NAME -> onView(withResourceName(containsString(selector.value)))

            SelectorStrategy.UIAUTOMATOR2_BY_CLASS -> {
                val obj = mDevice.findObject(By.clazz(selector.value))
                if (obj == null) {
                    Log.i("mozGetElement", "UIObject2 not found for res: ${selector.value}")
                    null
                } else {
                    obj
                }
            }

            SelectorStrategy.UIAUTOMATOR2_BY_TEXT -> {
                val obj = mDevice.findObject(By.text(selector.value))
                if (obj == null) {
                    Log.i("mozGetElement", "UIObject2 not found for res: ${selector.value}")
                    null
                } else {
                    obj
                }
            }

            SelectorStrategy.UIAUTOMATOR2_BY_TEXT_CONTAINS -> {
                val obj = mDevice.findObject(By.textContains(selector.value))
                if (obj == null) {
                    Log.i("mozGetElement", "UIObject2 not found for textContains: ${selector.value}")
                    null
                } else {
                    obj
                }
            }

            SelectorStrategy.UIAUTOMATOR2_BY_DESCRIPTION_CONTAINS -> {
                val obj = mDevice.findObject(By.descContains(selector.value))
                if (obj == null) {
                    Log.i("mozGetElement", "UIObject2 not found for descContains: ${selector.value}")
                    null
                } else {
                    obj
                }
            }

            SelectorStrategy.UIAUTOMATOR2_BY_RAW_RES -> {
                val obj = mDevice.findObject(By.res(selector.value))
                if (obj == null) {
                    Log.i("mozGetElement", "UIObject2 not found for raw res: ${selector.value}")
                    null
                } else {
                    obj
                }
            }

            SelectorStrategy.UIAUTOMATOR2_BY_RES -> {
                val obj = mDevice.findObject(By.res(packageName + ":id/" + selector.value))
                if (obj == null) {
                    Log.i("mozGetElement", "UIObject2 not found for res: ${selector.value}")
                    null
                } else {
                    obj
                }
            }

            SelectorStrategy.UIAUTOMATOR_WITH_RES_ID -> {
                val obj = mDevice.findObject(UiSelector().resourceId(packageName + ":id/" + selector.value))
                if (!obj.exists()) null else obj
            }

            SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG -> {
                val obj = mDevice.findObject(UiSelector().resourceId(selector.value))
                if (!obj.exists()) null else obj
            }

            SelectorStrategy.UIAUTOMATOR_WITH_RES_ID_AND_DESCRIPTION_CONTAINS -> {
                val descriptionToMatch = selector.secondaryValue ?: ""
                val obj =
                    mDevice.findObject(UiSelector().resourceId(selector.value).descriptionContains(descriptionToMatch))
                if (!obj.exists()) null else obj
            }

            SelectorStrategy.UIAUTOMATOR_WITH_TEXT -> {
                val obj = mDevice.findObject(UiSelector().text(selector.value))
                if (!obj.exists()) null else obj
            }

            SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS -> {
                val obj = mDevice.findObject(UiSelector().textContains(selector.value))
                if (!obj.exists()) null else obj
            }

            SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS -> {
                val obj = mDevice.findObject(UiSelector().descriptionContains(selector.value))
                if (!obj.exists()) null else obj
            }

            SelectorStrategy.UIAUTOMATOR_WITH_RES_ID_AND_TEXT -> {
                val textToMatch = selector.secondaryValue ?: ""

                val fullResId = packageName + ":id/" + selector.value

                val obj = mDevice.findObject(UiSelector().resourceId(fullResId).text(textToMatch))

                if (!obj.exists()) null else obj
            }

            SelectorStrategy.UIAUTOMATOR_WITH_RES_ID_CONTAINING_TEXT -> {
                val textToMatch = selector.secondaryValue ?: ""
                val fullResId = packageName + ":id/" + selector.value
                val obj = mDevice.findObject(UiSelector().resourceId(fullResId).textContains(textToMatch))
                if (!obj.exists()) null else obj
            }

            // Res-id used verbatim — no packageName prefix — for system-UI ids we do not own.
            SelectorStrategy.UIAUTOMATOR_WITH_RAW_RES_ID_CONTAINING_TEXT -> {
                val textToMatch = selector.secondaryValue ?: ""
                val obj = mDevice.findObject(UiSelector().resourceId(selector.value).textContains(textToMatch))
                if (!obj.exists()) null else obj
            }

            SelectorStrategy.UIAUTOMATOR_WITH_RAW_RES_ID -> {
                val obj = mDevice.findObject(UiSelector().resourceId(selector.value))
                if (!obj.exists()) null else obj
            }

            SelectorStrategy.UIAUTOMATOR_WITH_WEB_ID_AND_TEXT -> {
                val textToMatch = selector.secondaryValue ?: ""
                // Raw web DOM id (no package prefix), matched together with exact text.
                val obj = mDevice.findObject(UiSelector().resourceId(selector.value).text(textToMatch))
                if (!obj.exists()) null else obj
            }

            SelectorStrategy.COMPOSE_BY_TEXT_SUBSTRING -> {
                val node =
                    composeRule.onAllNodesWithText(selector.value, substring = true, useUnmergedTree = true).onFirst()
                try {
                    node.assertExists()
                    node
                } catch (_: AssertionError) {
                    Log.i("mozGetElement", "Compose node not found for text substring: ${selector.value}")
                    null
                }
            }
        }
    }

    private fun mozVerifyElement(selector: Selector, applyPreconditions: Boolean = true): Boolean {
        // This is a *presence check*: it answers "is this element displayed right now?" and must
        // NEVER throw. Callers rely on that contract — mozIsOnPageNow()/mozWaitForPageToLoad() poll
        // it before navigation even starts. If it throws (e.g. mozGetElement hits an ambiguous match,
        // or a backend asserts eagerly), that exception escapes into navigateToPage(), which on real
        // hardware triggers the failure-screenshot path and a StrictMode penaltyDeath that MASKS the
        // real error. So we resolve + probe entirely inside try/catch and degrade to `false`.
        return try {
            val element = mozGetElement(selector, applyPreconditions = applyPreconditions)

            when (element) {
                is ViewInteraction -> {
                    try {
                        element.check(matches(isDisplayed()))
                        true
                    } catch (_: Throwable) {
                        false
                    }
                }
                is UiObject -> element.exists()
                is UiObject2 -> true
                is SemanticsNodeInteraction -> {
                    try {
                        element.assertExists()
                        element.assertIsDisplayed()
                        true
                    } catch (_: Throwable) {
                        false
                    }
                }
                else -> false
            }
        } catch (_: Throwable) {
            false
        }
    }

    // ------------------------------------------------------------
    // Preconditions (CMD)
    // ------------------------------------------------------------

    private fun requiresScroll(groups: List<String>): Boolean {
        return groups.any {
            it.equals("requiresScroll", ignoreCase = true) || it.equals("needsSwipeNavStep", ignoreCase = true)
        }
    }

    private fun desiredSwipeDirection(groups: List<String>): SwipeDirection {
        return when {
            groups.any { it.equals("swipeDown", true) } -> SwipeDirection.DOWN
            groups.any { it.equals("swipeLeft", true) } -> SwipeDirection.LEFT
            groups.any { it.equals("swipeRight", true) } -> SwipeDirection.RIGHT
            else -> SwipeDirection.UP
        }
    }

    fun mozClear(selector: Selector): BasePage {
        // TODO (I. RIOS 3/20/2026): pull out boiler plate setup in separate method
        val rep = rep()
        rep?.startCmd(
            safeId("clear_text", selector.description),
            "Attempting to clear text from '${selector.description}'...",
            1,
        )
        rep?.startLoc(safeId("loc", selector.description), "Attempting to locate '${selector.description}'...", 2)

        val element = mozGetElement(selector)
        if (element == null) {
            rep?.endLoc(success = false, message = notFound(selector.description))
            rep?.endCmd(success = false, message = "Clear text failed: element not found ('${selector.description}')")
            throw AssertionError(
                "Element not found for selector: ${selector.description} (${selector.strategy} -> ${selector.value})"
            )
        } else {
            rep?.endLoc(success = true, message = found(selector.description))
        }

        try {
            when (element) {
                is SemanticsNodeInteraction -> element.performTextClearance()
                is ViewInteraction -> element.perform(clearText())
                is UiObject -> element.clearTextField()
                is UiObject2 -> element.clear()
                else ->
                    throw AssertionError(
                        "Unsupported element type (${element::class.simpleName}) for selector: ${selector.description}"
                    )
            }

            rep?.endCmd(success = true, message = "Cleared text from '${selector.description}'")
            return this
        } catch (e: Throwable) {
            rep?.endCmd(
                success = false,
                message = "Clear text failed for '${selector.description}': ${e.message ?: "exception"}",
            )
            throw AssertionError("Failed to clear text for selector: ${selector.description}", e)
        }
    }

    fun mozClearAndEnterText(text: String, selector: Selector): BasePage {
        mozClear(selector)
        return mozEnterText(text, selector)
    }

    fun mozVerifyFileOpensInExternalApp(appPackageName: String): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("verify_file_opens_in_external_app", appPackageName),
            "Verifying external app '$appPackageName' opens...",
        )
        try {
            AppAndSystemHelper.assertExternalAppOpens(appPackageName)
            rep?.endCmd(success = true, message = "Asserted external app '$appPackageName' open flow")
        } catch (e: Throwable) {
            rep?.endCmd(success = false, message = "External app assertion failed for '$appPackageName': ${e.message}")
            throw e
        }
        return this
    }

    /**
     * Asserts the native app [appPackageName] launches, then force-stops it so it doesn't linger into subsequent tests.
     * Falls back to verifying [url] if the package isn't installed. Delegates to
     * [AppAndSystemHelper.assertNativeAppOpens].
     */
    fun mozVerifyNativeAppOpens(appPackageName: String, url: String = ""): BasePage {
        val rep = rep()
        rep?.startCmd(
            safeId("verify_native_app_opens", appPackageName),
            "Verifying native app '$appPackageName' opens...",
        )
        try {
            AppAndSystemHelper.assertNativeAppOpens(composeRule, appPackageName, url)
            rep?.endCmd(success = true, message = "Verified native app '$appPackageName' open flow")
        } catch (e: Throwable) {
            rep?.endCmd(success = false, message = "Native app verification failed for '$appPackageName': ${e.message}")
            throw e
        }
        return this
    }

    private fun ensureReachable(selector: Selector) {
        val rep = rep()

        if (requiresScroll(selector.groups)) {
            val dir = desiredSwipeDirection(selector.groups)

            rep?.startCmd(
                safeId("precondition_scroll", selector.description),
                "Attempting to bring '${selector.description}' into view (swipe ${dir.name.lowercase()})...",
                1,
            )
            Log.i("Preconditions", "'${selector.description}' requires scroll. Swiping $dir to bring into view.")

            // IMPORTANT: do not allow nested preconditions during swipe-to lookup
            try {
                mozSwipeTo(selector, direction = dir, maxSwipes = 10, applyPreconditions = false)
                rep?.endCmd(success = true, message = "Precondition satisfied for '${selector.description}'")
            } catch (t: Throwable) {
                rep?.endCmd(
                    success = false,
                    message = "Precondition failed for '${selector.description}': ${t.message ?: "exception"}",
                )
                throw t
            }
        }
    }

    /**
     * Hide the soft keyboard, tolerating Espresso's failure to do so.
     *
     * closeSoftKeyboard() throws PerformException when the focused view's root is not the one it expects, which happens
     * whenever no editable view has focus - on the homepage, or after a dialog takes focus. Hiding the keyboard is only
     * ever a convenience for the assertion that follows, so failing to do it must not fail the test. Protected so page
     * objects can use it in place of a bare call.
     */
    protected fun dismissSoftKeyboard() {
        runCatching { closeSoftKeyboard() }
            .onFailure { Log.i("BasePage", "dismissSoftKeyboard: ignored ${it::class.java.simpleName}") }
    }
}
