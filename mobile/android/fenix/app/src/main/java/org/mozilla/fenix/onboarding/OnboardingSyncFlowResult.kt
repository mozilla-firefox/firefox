/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding

import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavController
import kotlinx.coroutines.flow.StateFlow

/**
 * Let sync flows started from an onboarding sync page (for example [org.mozilla.fenix.settings.PairFragment]) report
 * that they completed so that the onboarding can move past that page.
 *
 * The result is stored in the [androidx.lifecycle.SavedStateHandle] of the previous destination.
 */
internal object OnboardingSyncFlowResult {
    private const val KEY = "onboarding_sync_flow_completed"

    /** Reports to the previous destination that the flow completed. This must be called before popping the flow. */
    fun setCompleted(navController: NavController) {
        navController.previousBackStackEntry?.savedStateHandle?.set(KEY, true)
    }

    /** Emit whether a sync flow started from [entry] was completed. */
    fun isCompleted(entry: NavBackStackEntry): StateFlow<Boolean> = entry.savedStateHandle.getStateFlow(KEY, false)

    /** Marks the completion as handled. */
    fun consume(entry: NavBackStackEntry) {
        entry.savedStateHandle[KEY] = false
    }
}
