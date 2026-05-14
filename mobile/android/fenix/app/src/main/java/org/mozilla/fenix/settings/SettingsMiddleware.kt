/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.launch
import mozilla.components.concept.ai.controls.AIFeatureRegistry
import mozilla.components.feature.summarize.settings.SummarizationSettings
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

/**
 * Middleware that observes external sources (AI feature registry, summarization settings)
 * and dispatches load actions into the settings [SettingsStore].
 */
class SettingsMiddleware(
    val featureRegistry: AIFeatureRegistry,
    val summarizationSettings: SummarizationSettings,
    val scope: CoroutineScope,
) : Middleware<SettingsState, SettingsAction> {
    override fun invoke(
        store: Store<SettingsState, SettingsAction>,
        next: (SettingsAction) -> Unit,
        action: SettingsAction,
    ) {
        next(action)
        when (action) {
            is SettingsAction.SettingsViewCreated -> {
                featureRegistry.getFeatures().forEach { feature ->
                    scope.launch {
                        feature.isEnabled.collect {
                            store.dispatch(SettingsAction.AIControlsFeatureStateLoaded(it, feature.id))
                        }
                    }
                }
                scope.launch {
                    combine(
                        summarizationSettings.getFeatureEnabledUserStatus(),
                        summarizationSettings.getGestureEnabledUserStatus(),
                    ) { isFeatureEnabled, isGestureEnabled ->
                        SettingsAction.PageSummariesSettingsLoaded(
                            isFeatureEnabled = isFeatureEnabled,
                            isGestureEnabled = isGestureEnabled,
                        )
                    }.collect { store.dispatch(it) }
                }
            }

            is SettingsAction.AIControlsFeatureStateLoaded -> Unit
            is SettingsAction.PageSummariesSettingsLoaded -> Unit
        }
    }
}
