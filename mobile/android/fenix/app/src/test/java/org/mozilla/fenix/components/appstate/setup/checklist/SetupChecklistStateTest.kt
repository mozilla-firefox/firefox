/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.appstate.setup.checklist

import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class SetupChecklistStateTest {
    @Test
    fun `WHEN state is initialized THEN the initial state is correct`() {
        val expected = SetupChecklistState(
            checklistItems = emptyList(),
            progress = Progress(
                totalTasks = 0,
                completedTasks = 0,
            ),
        )
        assertEquals(expected, SetupChecklistState())
    }

    @Test
    fun `GIVEN all tasks completed WHEN getting checklist title THEN returns completed title`() {
        val expected = getSetupChecklistTitle(testContext, allTasksCompleted = true)
        assertEquals("Congratulations!", expected)
    }

    @Test
    fun `GIVEN tasks not completed WHEN getting checklist title THEN returns incomplete title`() {
        val expected = getSetupChecklistTitle(testContext, allTasksCompleted = false)
        assertEquals("Finish setting up Firefox", expected)
    }

    @Test
    fun `GIVEN tab strip disabled when calling getSetupChecklistSubtitle then returns expected subtitles`() {
        val step0Text = "Complete all 6 steps to set up Firefox for the best browsing experience."
        val step1Text = "Great start! You’ve completed 1 out of 6 steps."
        val step2Text = "You’ve completed 2 out of 6 steps. Great progress!"
        val step3Text = "You’re halfway there! Three steps finished and 3 to go."
        val step4Text = "You’re 4 steps in. Only 2 more to go!"
        val step5Text = "Almost there! You’re just 1 step away from the finish line."
        val step6Text =
            "You’ve completed all 6 setup steps. Enjoy the speed, privacy, and security of Firefox."

        assertEquals(step0Text, getSubtitleForGroupWith6Tasks(0))
        assertEquals(step1Text, getSubtitleForGroupWith6Tasks(1))
        assertEquals(step2Text, getSubtitleForGroupWith6Tasks(2))
        assertEquals(step3Text, getSubtitleForGroupWith6Tasks(3))
        assertEquals(step4Text, getSubtitleForGroupWith6Tasks(4))
        assertEquals(step5Text, getSubtitleForGroupWith6Tasks(5))
        assertEquals(step6Text, getSubtitleForGroupWith6Tasks(6))
        assertEquals(null, getSubtitleForGroupWith6Tasks(7))
    }

    private fun getSubtitleForGroupWith6Tasks(completedTasks: Int) =
        getBasicSubtitleForGroup(6, completedTasks)

    @Test
    fun `GIVEN tab strip enabled when calling getSetupChecklistSubtitle then returns expected subtitles`() {
        val step0Text = "Complete all 5 steps to set up Firefox for the best browsing experience."
        val step1Text = "Great start! You’ve completed 1 out of 5 steps."
        val step2Text = "You’ve completed 2 out of 5 steps. Great progress!"
        val step3Text = "You’re halfway there! Three steps finished and 2 to go."
        val step4Text = "Almost there! You’re just 1 step away from the finish line."
        val step5Text =
            "You’ve completed all 5 setup steps. Enjoy the speed, privacy, and security of Firefox."

        assertEquals(step0Text, getSubtitleForGroupWith5Tasks(0))
        assertEquals(step1Text, getSubtitleForGroupWith5Tasks(1))
        assertEquals(step2Text, getSubtitleForGroupWith5Tasks(2))
        assertEquals(step3Text, getSubtitleForGroupWith5Tasks(3))
        assertEquals(step4Text, getSubtitleForGroupWith5Tasks(4))
        assertEquals(step5Text, getSubtitleForGroupWith5Tasks(5))
        assertEquals(null, getSubtitleForGroupWith5Tasks(6))
    }

    private fun getSubtitleForGroupWith5Tasks(completedTasks: Int) =
        getBasicSubtitleForGroup(5, completedTasks)

    @Test
    fun `GIVEN total tasks is less than 5 and more 6 getSetupChecklistSubtitle then returns null`() {
        assertEquals(null, getBasicSubtitleForGroup(4, 0))
        assertEquals(null, getBasicSubtitleForGroup(7, 0))
    }

    private fun getBasicSubtitleForGroup(totalTasks: Int, completedTasks: Int) =
        getSetupChecklistSubtitle(
            context = testContext,
            progress = Progress(totalTasks, completedTasks),
            isGroups = true,
        )
}
