/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.emitAll
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import mozilla.components.feature.summarize.settings.SummarizationSettings

/**
 * Eagerly mirrors the user's [SummarizationSettings] preferences into hot [StateFlow]s so the UI
 * can read the latest value synchronously via `.value` instead of showing a placeholder while the
 * underlying DataStore flow loads. Persistence still goes through [settings].
 */
class SummarizationSettingsCache(
    private val settings: SummarizationSettings,
    scope: CoroutineScope,
) {
    val featureEnabled: StateFlow<Boolean> =
        flow { emitAll(settings.getFeatureEnabledUserStatus()) }
            .map { it == true }
            .stateIn(scope, SharingStarted.Eagerly, false)

    val gestureEnabled: StateFlow<Boolean> =
        flow { emitAll(settings.getGestureEnabledUserStatus()) }
            .stateIn(scope, SharingStarted.Eagerly, true)
}
