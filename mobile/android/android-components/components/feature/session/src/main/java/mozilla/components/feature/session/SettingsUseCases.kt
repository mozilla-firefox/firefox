/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.session

import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.EngineSession.TrackingProtectionPolicy
import mozilla.components.concept.engine.Settings

/**
 * Contains use cases related to engine [Settings].
 *
 * @param engine reference to the application's browser [Engine].
 * @param store the application's [BrowserStore].
 */
class SettingsUseCases(
    engine: Engine,
    store: BrowserStore,
) {
    /**
     * Updates the tracking protection policy to the given policy value when invoked. All active sessions are
     * automatically updated with the new policy.
     */
    class UpdateTrackingProtectionUseCase
    internal constructor(
        private val engine: Engine,
        private val store: BrowserStore,
    ) {
        /** Updates the tracking protection policy for all current and future [EngineSession] instances. */
        operator fun invoke(policy: TrackingProtectionPolicy) {
            engine.settings.trackingProtectionPolicy = policy

            store.state.forEachEngineSession { engineSession ->
                engineSession.updateTrackingProtection(policy)
            }

            engine.clearSpeculativeSession()
        }
    }

    val updateTrackingProtection: UpdateTrackingProtectionUseCase by lazy {
        UpdateTrackingProtectionUseCase(engine, store)
    }
}

private fun BrowserState.forEachEngineSession(block: (EngineSession) -> Unit) {
    (tabs + customTabs).mapNotNull { it.engineState.engineSession }.forEach(block)
}
