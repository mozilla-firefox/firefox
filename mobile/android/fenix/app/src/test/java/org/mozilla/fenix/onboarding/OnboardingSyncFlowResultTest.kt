/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding

import androidx.lifecycle.SavedStateHandle
import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavController
import io.mockk.every
import io.mockk.mockk
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import org.junit.Test

class OnboardingSyncFlowResultTest {

    private fun entryWith(savedStateHandle: SavedStateHandle): NavBackStackEntry = mockk {
        every { this@mockk.savedStateHandle } returns savedStateHandle
    }

    @Test
    fun `GIVEN no completion has been reported WHEN isCompleted is called THEN it emits false`() {
        val entry = entryWith(SavedStateHandle())

        assertFalse(OnboardingSyncFlowResult.isCompleted(entry).value)
    }

    @Test
    fun `GIVEN a previous and a current destination WHEN setCompleted is called THEN only the previous destination is notified`() {
        val previousEntry = entryWith(SavedStateHandle())
        val currentEntry = entryWith(SavedStateHandle())
        val navController: NavController = mockk {
            every { previousBackStackEntry } returns previousEntry
        }

        OnboardingSyncFlowResult.setCompleted(navController)

        assertTrue(OnboardingSyncFlowResult.isCompleted(previousEntry).value)
        assertFalse(OnboardingSyncFlowResult.isCompleted(currentEntry).value)
    }

    @Test
    fun `GIVEN an observed completion flow WHEN consume is called THEN the flow emits false`() {
        val entry = entryWith(SavedStateHandle())
        val navController: NavController = mockk {
            every { previousBackStackEntry } returns entry
        }
        val isCompleted = OnboardingSyncFlowResult.isCompleted(entry)

        OnboardingSyncFlowResult.setCompleted(navController)
        assertTrue(isCompleted.value)

        OnboardingSyncFlowResult.consume(entry)
        assertFalse(isCompleted.value)
    }
}
