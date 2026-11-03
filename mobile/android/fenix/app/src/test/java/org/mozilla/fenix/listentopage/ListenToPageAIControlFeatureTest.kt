/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.listentopage

import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.ai.controls.AIFeatureState
import mozilla.components.concept.ai.controls.isEnabled
import mozilla.components.feature.listentopage.settings.ListenEnabledState
import mozilla.components.feature.listentopage.settings.ListenSettings
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class ListenToPageAIControlFeatureTest {

    @Test
    fun `featureState reflects the enabled state from settings`() = runTest {
        assertEquals(AIFeatureState.Enabled, featureStateFor(ListenEnabledState.Enabled))
        assertEquals(AIFeatureState.Disabled, featureStateFor(ListenEnabledState.Disabled))
    }

    @Test
    fun `featureState is unknown when the user has never set it`() = runTest {
        assertEquals(AIFeatureState.Unknown, featureStateFor(ListenEnabledState.Unset))
    }

    @Test
    fun `set persists to settings`() = runTest {
        val settings = ListenSettings.inMemory(enabled = ListenEnabledState.Enabled)
        val feature = ListenToPageAIControlFeature(settings)

        feature.set(false)

        assertEquals(ListenEnabledState.Disabled, settings.enabled.value)
        assertFalse(feature.isEnabled.first())
    }

    @Test
    fun `featureState does not emit until settings have loaded`() = runTest {
        val settings = ListenSettings.inMemory(enabled = ListenEnabledState.Loading)
        val feature = ListenToPageAIControlFeature(settings)
        val states = mutableListOf<AIFeatureState>()
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { feature.featureState.toList(states) }

        assertTrue(states.isEmpty())

        settings.setEnabled(true)

        assertEquals(listOf(AIFeatureState.Enabled), states)
    }

    private suspend fun featureStateFor(enabled: ListenEnabledState) =
        ListenToPageAIControlFeature(ListenSettings.inMemory(enabled = enabled)).featureState.first()
}
