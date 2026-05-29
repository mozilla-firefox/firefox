/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.summarization

import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.coroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.mapNotNull
import kotlinx.coroutines.launch
import mozilla.components.feature.summarize.settings.SummarizationSettings

/**
 * See [FenixSummarizationSettingsBinding].
 */
interface SummarizationSettingsBinding {
    val isFeatureEnabled: StateFlow<Boolean>
    val isGestureEnabled: StateFlow<Boolean>
}

/**
 * Wrapper for the summarization settings managed by the module. This is a convenience class to bridge
 * suspending and non-suspending contents, to be hosted by a lifecycle observer.
 */
class FenixSummarizationSettingsBinding(
    private val summarizationSettings: SummarizationSettings,
) : DefaultLifecycleObserver, SummarizationSettingsBinding {
    private val _isFeatureEnabled = MutableStateFlow(false)
    override val isFeatureEnabled: StateFlow<Boolean> = _isFeatureEnabled
    private val _isGestureEnabled = MutableStateFlow(false)
    override val isGestureEnabled: StateFlow<Boolean> = _isGestureEnabled

    override fun onCreate(owner: LifecycleOwner) {
        super.onCreate(owner)
        owner.lifecycle.coroutineScope.launch {
            combine(
                summarizationSettings.getFeatureEnabledUserStatus()
                    .mapNotNull { it },
                summarizationSettings.getGestureEnabledUserStatus(),
            ) { a, b ->
                a to b
            }
                .distinctUntilChanged()
                .collect { (isFeatureEnabled, isGestureEnabled) ->
                    this@FenixSummarizationSettingsBinding._isFeatureEnabled.value = isFeatureEnabled
                    this@FenixSummarizationSettingsBinding._isGestureEnabled.value = isGestureEnabled
                }
        }
    }
}
