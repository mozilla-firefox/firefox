/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.core

import android.view.View
import androidx.compose.ui.test.SemanticsNodeInteraction
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.longClick
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performImeAction
import androidx.compose.ui.test.performTextClearance
import androidx.compose.ui.test.performTextInput
import androidx.compose.ui.test.performTouchInput
import androidx.test.espresso.UiController
import androidx.test.espresso.ViewAction
import androidx.test.espresso.ViewInteraction
import androidx.test.espresso.action.ViewActions
import androidx.test.espresso.matcher.ViewMatchers.isEnabled
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.uiautomator.UiDevice
import androidx.test.uiautomator.UiObject
import androidx.test.uiautomator.UiObject2
import org.hamcrest.Matcher
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeShort

/**
 * Do a thing to a located element, whatever backend it came from, so a verb names its action and nothing else. One copy
 * of the four-way dispatch: a backend a verb would have forgotten to handle does the obvious thing instead of failing
 * as "unsupported element type".
 */
object UiActions {

    private val device: UiDevice
        get() = UiDevice.getInstance(InstrumentationRegistry.getInstrumentation())

    fun click(element: UiElement) {
        dispatch(
            element,
            "click",
            espresso = { it.perform(ViewActions.click()) },
            // A UiObject is resolved on one poll tick and acted on the next; on a screen still animating (e.g. the
            // launcher settling a freshly-added home-screen shortcut) the node can flicker out in that gap, so bridge
            // it with waitForExists rather than a single instantaneous exists() that throws on a transient miss.
            uiObject = {
                if (!it.waitForExists(waitingTimeShort)) throw AssertionError("UiObject does not exist")
                if (!it.click()) throw AssertionError("Failed to click UiObject")
            },
            uiObject2 = { it.click() },
            compose = {
                it.assertExists()
                it.assertIsDisplayed()
                it.performClick()
            },
        )
    }

    /**
     * Click by firing the view's click handler directly instead of tapping its on-screen coordinates. For an Espresso
     * view this climbs to the nearest clickable ancestor (or the view itself) and calls [View.performClick] - a text
     * selector on a `PreferenceFragment` row matches the label `TextView`, which has no click listener of its own (the
     * row's itemView does), and `performClick` does not bubble, so firing the ancestor is what actually selects the
     * option. Being coordinate-free, it is also immune to the dropped tap a list still settling from a momentum scroll
     * causes, which [click]'s coordinate tap suffers. The other backends have no equivalent hazard here, so they fall
     * back to their normal click.
     */
    fun performClick(element: UiElement) {
        dispatch(
            element,
            "perform click",
            espresso = { it.perform(clickableAncestorClick()) },
            uiObject = {
                if (!it.waitForExists(waitingTimeShort)) throw AssertionError("UiObject does not exist")
                if (!it.click()) throw AssertionError("Failed to click UiObject")
            },
            uiObject2 = { it.click() },
            compose = {
                it.assertExists()
                it.performClick()
            },
        )
    }

    private fun clickableAncestorClick(): ViewAction =
        object : ViewAction {
            override fun getConstraints(): Matcher<View> = isEnabled()

            override fun getDescription(): String = "click the nearest clickable ancestor's handler directly"

            override fun perform(uiController: UiController, view: View) {
                var target: View? = view
                while (target != null && !target.isClickable) {
                    target = target.parent as? View
                }
                (target ?: view).performClick()
                uiController.loopMainThreadUntilIdle()
            }
        }

    fun longClick(element: UiElement, durationMillis: Long = 5_000) =
        dispatch(
            element,
            "long click",
            espresso = { it.perform(ViewActions.longClick()) },
            uiObject = {
                if (!it.waitForExists(waitingTimeShort)) throw AssertionError("UiObject does not exist")
                if (!it.longClick()) throw AssertionError("Failed to long click UiObject")
            },
            uiObject2 = { it.longClick() },
            compose = {
                it.assertExists()
                it.assertIsDisplayed()
                it.performTouchInput { longClick(durationMillis = durationMillis) }
            },
        )

    fun enterText(element: UiElement, text: String) =
        dispatch(
            element,
            "enter text",
            espresso = { it.perform(ViewActions.typeText(text)) },
            uiObject = { it.setText(text) },
            uiObject2 = { it.setText(text) },
            compose = { it.performTextInput(text) },
        )

    fun clear(element: UiElement) =
        dispatch(
            element,
            "clear",
            espresso = { it.perform(ViewActions.clearText()) },
            uiObject = { it.clearTextField() },
            uiObject2 = { it.clear() },
            compose = { it.performTextClearance() },
        )

    // UiAutomator has no per-element IME action, so the key goes to the focused field via the device -
    // which is what the hand-written copies of this branch already did.
    fun pressEnter(element: UiElement) =
        dispatch(
            element,
            "press enter",
            espresso = { it.perform(ViewActions.pressImeActionButton()) },
            uiObject = { device.pressEnter() },
            uiObject2 = { device.pressEnter() },
            compose = { it.performImeAction() },
        )

    private inline fun dispatch(
        element: UiElement,
        action: String,
        espresso: (ViewInteraction) -> Unit,
        uiObject: (UiObject) -> Unit,
        uiObject2: (UiObject2) -> Unit,
        compose: (SemanticsNodeInteraction) -> Unit,
    ) {
        val raw = element.backend()
        when (raw) {
            is ViewInteraction -> espresso(raw)
            is UiObject -> uiObject(raw)
            is UiObject2 -> uiObject2(raw)
            is SemanticsNodeInteraction -> compose(raw)
            else -> unsupported(raw, action)
        }
    }

    private fun unsupported(element: Any, action: String): Nothing =
        throw AssertionError("Cannot $action a ${element::class.simpleName}")
}
