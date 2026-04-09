/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.translations

import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.TranslationsAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.TranslationsBrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.ai.controls.AIFeatureRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mozilla.fenix.components.metrics.fake.FakeLifecycleOwner

@OptIn(ExperimentalCoroutinesApi::class)
class TranslationsAIControllableFeatureRegistrarTest {

    private val lifecycleOwner = FakeLifecycleOwner()
    private val settings = TranslationsEnabledSettings.inMemory()

    @Test
    fun `WHEN engine is supported on resume THEN translation feature is registered`() = runTest {
        val registry = AIFeatureRegistry.inMemory()
        val browserStore = BrowserStore(BrowserState(translationEngine = TranslationsBrowserState(isEngineSupported = true)))
        val registrar = TranslationsAIControllableFeatureRegistrar(
            registry,
            browserStore,
            settings,
            this,
        )

        registrar.onResume(lifecycleOwner)
        this.runCurrent()

        assertEquals(1, registry.getFeatures().size)
        assertEquals(TranslationsAIControllableFeature.id, registry.getFeatures().first().id)
    }

    @Test
    fun `WHEN engine is not supported on resume THEN translation feature is not registered`() = runTest {
        val registry = AIFeatureRegistry.inMemory()
        val browserStore = BrowserStore(BrowserState(translationEngine = TranslationsBrowserState(isEngineSupported = false)))
        val registrar = TranslationsAIControllableFeatureRegistrar(
            registry,
            browserStore,
            settings,
            this,
        )

        registrar.onResume(lifecycleOwner)
        this.runCurrent()

        assertTrue(registry.getFeatures().isEmpty())
    }

    @Test
    fun `WHEN engine support is unknown on resume THEN translation feature is registered`() = runTest {
        val registry = AIFeatureRegistry.inMemory()
        val browserStore = BrowserStore(BrowserState(translationEngine = TranslationsBrowserState(isEngineSupported = null)))
        val registrar = TranslationsAIControllableFeatureRegistrar(
            registry,
            browserStore,
            settings,
            this,
        )

        registrar.onResume(lifecycleOwner)
        this.runCurrent()

        assertEquals(1, registry.getFeatures().size)
        assertEquals(TranslationsAIControllableFeature.id, registry.getFeatures().first().id)
    }

    @Test
    fun `WHEN engine becomes supported after resume THEN translation feature is registered`() = runTest {
        val registry = AIFeatureRegistry.inMemory()
        val browserStore = BrowserStore(BrowserState(translationEngine = TranslationsBrowserState(isEngineSupported = null)))
        val registrar = TranslationsAIControllableFeatureRegistrar(
            registry,
            browserStore,
            settings,
            this,
        )

        registrar.onResume(lifecycleOwner)
        this.runCurrent()
        assertEquals(1, registry.getFeatures().size)

        browserStore.dispatch(TranslationsAction.SetEngineSupportedAction(isEngineSupported = true))
        advanceUntilIdle()

        assertEquals(1, registry.getFeatures().size)
        assertEquals(TranslationsAIControllableFeature.id, registry.getFeatures().first().id)
    }

    @Test
    fun `WHEN feature is already registered and lifecycle resumes again THEN feature is not re-registered`() = runTest {
        val registry = AIFeatureRegistry.inMemory()
        val browserStore = BrowserStore(BrowserState(translationEngine = TranslationsBrowserState(isEngineSupported = true)))
        val registrar = TranslationsAIControllableFeatureRegistrar(
            registry,
            browserStore,
            settings,
            this,
        )

        registrar.onResume(lifecycleOwner)
        this.runCurrent()
        assertEquals(1, registry.getFeatures().size)

        registrar.onPause(lifecycleOwner)
        registrar.onResume(lifecycleOwner)
        this.runCurrent()

        assertEquals(1, registry.getFeatures().size)
    }

    @Test
    fun `WHEN paused THEN store state changes do not register the feature`() = runTest {
        val registry = AIFeatureRegistry.inMemory()
        val browserStore = BrowserStore(BrowserState(translationEngine = TranslationsBrowserState(isEngineSupported = false)))
        val registrar = TranslationsAIControllableFeatureRegistrar(
            registry,
            browserStore,
            settings,
            this,
        )

        registrar.onResume(lifecycleOwner)
        this.runCurrent()
        registrar.onPause(lifecycleOwner)

        browserStore.dispatch(TranslationsAction.SetEngineSupportedAction(isEngineSupported = true))
        advanceUntilIdle()

        assertTrue(registry.getFeatures().isEmpty())
    }

    @Test
    fun `WHEN paused and unpaused THEN store state is automatically collected`() = runTest {
        val registry = AIFeatureRegistry.inMemory()
        val browserStore = BrowserStore(BrowserState(translationEngine = TranslationsBrowserState(isEngineSupported = null)))
        val registrar = TranslationsAIControllableFeatureRegistrar(
            registry,
            browserStore,
            settings,
            this,
        )

        registrar.onResume(lifecycleOwner)
        registrar.onPause(lifecycleOwner)

        browserStore.dispatch(TranslationsAction.SetEngineSupportedAction(isEngineSupported = true))
        advanceUntilIdle()

        registrar.onResume(lifecycleOwner)
        this.runCurrent()

        assertEquals(1, registry.getFeatures().size)
    }
}
