/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.translations

import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.TranslationsAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.ai.controls.AIFeatureState
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class TranslationsAIControllableFeatureTest {

    private val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
    private val browserStore = BrowserStore(middleware = listOf(captureActionsMiddleware))

    @Test
    fun `isEnabled reflects settings`() = runTest {
        val settings = TranslationsEnabledSettings.inMemory(isEnabledInitial = true)
        val feature = TranslationsAIControllableFeature(settings, browserStore)

        assertTrue(feature.featureState.first() == AIFeatureState.Enabled)
    }

    @Test
    fun `set persists to settings and dispatches to browser store`() = runTest {
        val settings = TranslationsEnabledSettings.inMemory(isEnabledInitial = true)
        val feature = TranslationsAIControllableFeature(settings, browserStore)

        feature.set(false)

        assertTrue(feature.featureState.first() == AIFeatureState.Disabled)
        assertFalse(captureActionsMiddleware.findFirstAction(TranslationsAction.SetTranslationsEnabledAction::class).isTranslationsEnabled)
    }
}
