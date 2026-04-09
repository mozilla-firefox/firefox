/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.translations

import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.launch
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.ai.controls.AIFeatureRegistry

/**
 * A class to handle dynamic registration of the [TranslationsAIControllableFeature] after
 * [mozilla.components.browser.state.state.TranslationsBrowserState.isEngineSupported] becomes known.
 */
class TranslationsAIControllableFeatureRegistrar(
    private val aiRegistry: AIFeatureRegistry,
    private val browserStore: BrowserStore,
    private val translationsEnabledSettings: TranslationsEnabledSettings,
    private val scope: CoroutineScope,
) : DefaultLifecycleObserver {
    private var job: Job? = null

    override fun onResume(owner: LifecycleOwner) {
        super.onResume(owner)

        job = scope.launch {
            browserStore.stateFlow
                .distinctUntilChangedBy { it.translationEngine.isEngineSupported }
                .onEach { state ->
                    if (state.translationEngine.isEngineSupported != false) {
                        val isFeatureRegistered = aiRegistry
                            .getFeatures()
                            .any { it.id == TranslationsAIControllableFeature.id }
                        if (!isFeatureRegistered) {
                            aiRegistry.register(
                                TranslationsAIControllableFeature(
                                    settings = translationsEnabledSettings,
                                    browserStore = browserStore,
                                ),
                            )
                        }
                    }
                }
                .first()
        }
    }

    override fun onPause(owner: LifecycleOwner) {
        super.onPause(owner)
        job?.cancel()
    }
}
