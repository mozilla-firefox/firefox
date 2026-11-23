/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import android.text.format.DateUtils
import androidx.compose.ui.test.assertTextEquals
import androidx.compose.ui.test.hasTextExactly
import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import kotlin.test.assertIs
import mozilla.components.lib.crash.store.CrashAction
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.crashes.CrashActionDispatcher
import org.mozilla.fenix.crashes.UnsubmittedCrashDialog
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityTestRule
import org.mozilla.fenix.helpers.MatcherHelper
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.TestHelper

class UnsubmittedCrashDialogTest {

    @get:Rule(order = 0) val fenixTestRule: FenixTestRule = FenixTestRule()

    @get:Rule(order = 1)
    val composeTestRule =
        AndroidComposeTestRuleV2(HomeActivityTestRule.withDefaultSettingsOverrides()) {
            it.activity
        }

    private fun addCrashToStore(action: CrashAction) {
        composeTestRule.activityRule.activity.applicationContext.components.appStore.dispatch(
            AppAction.CrashActionWrapper(action)
        )
    }

    private fun verifyDialogText(text: String) = MatcherHelper.assertUIObjectExists(itemContainingText(text))

    private fun verifyDialogTextGone(text: String) = MatcherHelper.assertUIObjectIsGone(itemContainingText(text))

    private fun getUnsubmittedCrashNormal(): String =
        TestHelper.appContext.resources.getString(
            R.string.unsubmitted_crash_dialog_title_2,
            getStringResource(R.string.app_name),
        )

    private fun submitUnsubmittedCrashNormal(): String =
        TestHelper.appContext.resources.getString(R.string.unsubmitted_crash_dialog_positive_button_2)

    private fun cancelUnsubmittedCrashNormal(): String =
        TestHelper.appContext.resources.getString(R.string.unsubmitted_crash_dialog_negative_button_2)

    private fun submitUnsubmittedCrashPull(): String =
        TestHelper.appContext.resources.getString(R.string.unsubmitted_crash_dialog_positive_button)

    private fun cancelUnsubmittedCrashPull(): String =
        TestHelper.appContext.resources.getString(R.string.unsubmitted_crash_dialog_negative_button)

    private fun getUnsubmittedCrashPullOne(): String =
        TestHelper.appContext.resources.getString(
            R.string.unsubmitted_crash_requested_by_devs_dialog_title,
            getStringResource(R.string.app_name),
        )

    private fun getUnsubmittedCrashPullTwo(): String =
        TestHelper.appContext.resources.getString(
            R.string.unsubmitted_crashes_requested_by_devs_dialog_title,
            2,
            getStringResource(R.string.app_name),
        )

    private fun getUnsubmittedCrashPullThree(): String =
        TestHelper.appContext.resources.getString(
            R.string.unsubmitted_crashes_requested_by_devs_dialog_title,
            3,
            getStringResource(R.string.app_name),
        )

    private fun cancelForEverUnsubmittedCrashPull(): String =
        TestHelper.appContext.resources.getString(R.string.unsubmitted_crash_requested_by_devs_dialog_never_button)

    private fun clickButton(text: String) {
        composeTestRule.waitUntilAtLeastOneExists(hasTextExactly(text))
        val node = composeTestRule.onNodeWithText(text, false, ignoreCase = true)
        node.assertTextEquals(text)
        node.performClick()
    }

    @Test
    fun displayClassicDialogOn_CrashActionShowPrompt() {
        addCrashToStore(CrashAction.ShowPrompt())
        verifyDialogText(getUnsubmittedCrashNormal())
        verifyDialogText(cancelUnsubmittedCrashNormal())
        verifyDialogText(submitUnsubmittedCrashNormal())
    }

    @Test
    fun unsubmittedCrashDialog_ClickOnCancelDispatches_CrashActionCancelTapped_andDismissesDialog() {
        addCrashToStore(CrashAction.ShowPrompt())
        verifyDialogText(getUnsubmittedCrashNormal())
        clickButton(cancelUnsubmittedCrashNormal())
        verifyDialogTextGone(getUnsubmittedCrashNormal())
    }

    @Test
    fun unsubmittedCrashDialog_ClickOnCancelDispatches_CrashActionCancelTapped() {
        var dispatchedAction: CrashAction? = null
        val dialog = UnsubmittedCrashDialog.create(crashIDs = null)
        dialog.dispatcher = CrashActionDispatcher {
            dispatchedAction = it
        }
        dialog.show(composeTestRule.activityRule.activity.supportFragmentManager, UnsubmittedCrashDialog.TAG)

        verifyDialogText(getUnsubmittedCrashNormal())
        clickButton(cancelUnsubmittedCrashNormal())
        verifyDialogTextGone(getUnsubmittedCrashNormal())
        assertIs<CrashAction.CancelTapped>(dispatchedAction)
    }

    @Test
    fun unsubmittedCrashDialog_ClickOnSubmitDispatches_andDismissesDialog() {
        addCrashToStore(CrashAction.ShowPrompt())
        verifyDialogText(getUnsubmittedCrashNormal())
        verifyDialogText(submitUnsubmittedCrashNormal())
        clickButton(submitUnsubmittedCrashNormal())
        verifyDialogTextGone(getUnsubmittedCrashNormal())
    }

    @Test
    fun unsubmittedCrashDialog_ClickOnSubmitDispatches_CrashActionReportTapped() {
        var dispatchedAction: CrashAction? = null
        val dialog = UnsubmittedCrashDialog.create(crashIDs = null)
        dialog.dispatcher = CrashActionDispatcher {
            dispatchedAction = it
        }
        dialog.show(composeTestRule.activityRule.activity.supportFragmentManager, UnsubmittedCrashDialog.TAG)
        verifyDialogText(getUnsubmittedCrashNormal())
        clickButton(submitUnsubmittedCrashNormal())
        verifyDialogTextGone(getUnsubmittedCrashNormal())
        val report = dispatchedAction
        assertIs<CrashAction.ReportTapped>(report)
        assertFalse(report.automaticallySendChecked)
        assertTrue(report.crashIDs.isEmpty())
    }

    @Test
    fun displayCrashPullDialogOnForOneCrash_CrashActionPullCrashes_withOneCrashIDs() {
        addCrashToStore(CrashAction.CheckForCrashes(listOf("1")))
        verifyDialogText(getUnsubmittedCrashPullOne())
    }

    @Test
    fun displayCrashPullDialogOnForSeveralCrashes_CrashActionPullCrashes_withSeveralCrashIDs() {
        addCrashToStore(CrashAction.CheckForCrashes(listOf("1", "2")))
        verifyDialogText(getUnsubmittedCrashPullTwo())

        addCrashToStore(CrashAction.CheckForCrashes(listOf("1", "2", "3")))
        verifyDialogText(getUnsubmittedCrashPullThree())
    }

    @Test
    fun unsubmittedCrashDialog_PullingOneCrash_ClickOnCancelDispatches_andDismissesDialog() {
        addCrashToStore(CrashAction.CheckForCrashes(listOf("1")))
        verifyDialogText(getUnsubmittedCrashPullOne())
        clickButton(cancelUnsubmittedCrashPull().uppercase())
        verifyDialogTextGone(getUnsubmittedCrashPullOne())
    }

    @Test
    fun unsubmittedCrashDialog_PullingOneCrash_ClickOnCancelDispatches_CrashActionCancelTapped() {
        var dispatchedAction: CrashAction? = null
        val dialog = UnsubmittedCrashDialog.create(crashIDs = listOf("1"))
        dialog.dispatcher = CrashActionDispatcher {
            dispatchedAction = it
        }
        dialog.show(composeTestRule.activityRule.activity.supportFragmentManager, UnsubmittedCrashDialog.TAG)
        verifyDialogText(getUnsubmittedCrashPullOne())
        clickButton(cancelUnsubmittedCrashPull().uppercase())
        verifyDialogTextGone(getUnsubmittedCrashPullOne())
        assertIs<CrashAction.CancelTapped>(dispatchedAction)
    }

    @Test
    fun unsubmittedCrashDialog_PullingOneCrash_ClickOnCancelForEverDispatches_andDismissesDialog() {
        addCrashToStore(CrashAction.CheckForCrashes(listOf("1")))
        verifyDialogText(getUnsubmittedCrashPullOne())
        clickButton(cancelForEverUnsubmittedCrashPull().uppercase())
        verifyDialogTextGone(getUnsubmittedCrashPullOne())
    }

    @Test
    fun unsubmittedCrashDialog_PullingOneCrash_ClickOnCancelForEverDispatches_CrashActionCancelForEverTapped() {
        var dispatchedAction: CrashAction? = null
        val dialog = UnsubmittedCrashDialog.create(crashIDs = listOf("1"))
        dialog.dispatcher = CrashActionDispatcher {
            dispatchedAction = it
        }
        dialog.show(composeTestRule.activityRule.activity.supportFragmentManager, UnsubmittedCrashDialog.TAG)

        verifyDialogText(getUnsubmittedCrashPullOne())
        clickButton(cancelForEverUnsubmittedCrashPull().uppercase())
        verifyDialogTextGone(getUnsubmittedCrashPullOne())
        assertIs<CrashAction.CancelForEverTapped>(dispatchedAction)
    }

    @Test
    fun unsubmittedCrashDialog_PullingOneCrash_ClickOnSubmitDispatches_CrashActionReportTapped() {
        var dispatchedAction: CrashAction? = null
        val dialog = UnsubmittedCrashDialog.create(crashIDs = listOf("1"))
        dialog.dispatcher = CrashActionDispatcher {
            dispatchedAction = it
        }
        dialog.show(composeTestRule.activityRule.activity.supportFragmentManager, UnsubmittedCrashDialog.TAG)
        verifyDialogText(getUnsubmittedCrashPullOne())
        clickButton(submitUnsubmittedCrashPull().uppercase())
        verifyDialogTextGone(getUnsubmittedCrashPullOne())
        val report = dispatchedAction
        assertIs<CrashAction.ReportTapped>(report)
        assertFalse(report.automaticallySendChecked)
        assertEquals(report.crashIDs, listOf("1"))
    }

    @Test
    fun unsubmittedCrashDialog_PullingOneCrash_ClickOnSubmit_UpdateDontShowBefore() {
        var dontShowBeforeValue = TestHelper.appContext.components.settings.crashPullDontShowBefore

        addCrashToStore(CrashAction.CheckDeferred(listOf("1")))
        verifyDialogText(getUnsubmittedCrashPullOne())
        clickButton(submitUnsubmittedCrashPull().uppercase())
        verifyDialogTextGone(getUnsubmittedCrashPullOne())

        var newDate = System.currentTimeMillis()
        var dontShowBeforeValueSubmit = TestHelper.appContext.components.settings.crashPullDontShowBefore
        var expectedDontShowBeforeValueSubmitUp = newDate + 6 * DateUtils.DAY_IN_MILLIS
        var expectedDontShowBeforeValueSubmitDown = newDate + 8 * DateUtils.DAY_IN_MILLIS
        assertTrue(
            "$dontShowBeforeValueSubmit >= $expectedDontShowBeforeValueSubmitUp && $dontShowBeforeValueSubmit <= $expectedDontShowBeforeValueSubmitDown",
            dontShowBeforeValueSubmit >= expectedDontShowBeforeValueSubmitUp &&
                dontShowBeforeValueSubmit <= expectedDontShowBeforeValueSubmitDown,
        )
    }

    @Test
    fun unsubmittedCrashDialog_PullingOneCrash_DontShowBefore_PreviousWeek_NotBlocked() {
        var dontShowBeforeValue = TestHelper.appContext.components.settings.crashReportDeferredUntil
        var oneWeekBefore = System.currentTimeMillis() - 7 * DateUtils.DAY_IN_MILLIS
        TestHelper.appContext.components.settings.crashReportDeferredUntil = oneWeekBefore

        addCrashToStore(CrashAction.CheckDeferred(listOf("1")))
        verifyDialogText(getUnsubmittedCrashPullOne())
        clickButton(submitUnsubmittedCrashPull().uppercase())
        verifyDialogTextGone(getUnsubmittedCrashPullOne())

        TestHelper.appContext.components.settings.crashReportDeferredUntil = dontShowBeforeValue
    }

    @Test
    fun unsubmittedCrashDialog_PullingOneCrash_DontShowBefore_NextWeek_Blocked() {
        var dontShowBeforeValue = TestHelper.appContext.components.settings.crashPullDontShowBefore
        var oneWeekAfter = System.currentTimeMillis() + 7 * DateUtils.DAY_IN_MILLIS
        TestHelper.appContext.components.settings.crashPullDontShowBefore = oneWeekAfter

        addCrashToStore(CrashAction.CheckDeferred(listOf("1")))
        composeTestRule.waitUntilDoesNotExist(hasTextExactly(getUnsubmittedCrashPullOne()))

        TestHelper.appContext.components.settings.crashPullDontShowBefore = dontShowBeforeValue
    }
}
