/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection.store

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.state.Authorized
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.lib.state.helpers.AbstractBinding
import org.mozilla.fenix.GleanMetrics.Vpn
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction

/**
 * Error messages displayed to the user when IP protection operations encounter errors.
 *
 * @property dataLimitReached Message shown when the user has reached their monthly data limit.
 */
data class ErrorMessages(val dataLimitReached: String)

/**
 * Monitors [IPProtectionStore] state changes and displays informational sticky snackbars to the user via [AppStore].
 * This differs from the [IPProtectionSnackbarMiddleware] which is similar, but handles one-shot snackbar messages.
 *
 * @param store The IP protection store to observe for state changes.
 * @param appStore The app store used to dispatch snackbar actions.
 * @param errorMessages Localized error messages to display in snackbars.
 * @param repository [IPProtectionRepository] providing access to IP Protection related
 *   [android.content.SharedPreferences].
 * @param mainDispatcher the dispatcher to run asynchronous work on.
 */
class IPProtectionInfoPrompter(
    store: IPProtectionStore,
    private val appStore: AppStore,
    private val errorMessages: ErrorMessages,
    private val repository: IPProtectionRepository,
    mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : AbstractBinding<IPProtectionState>(store, mainDispatcher) {
    override suspend fun onState(flow: Flow<IPProtectionState>) {
        flow
            .distinctUntilChanged { old, new ->
                old.proxyStatus == new.proxyStatus &&
                    old.eligibilityStatus == new.eligibilityStatus &&
                    old.usageQuota?.resetTime == new.usageQuota?.resetTime
            }
            .collect { state ->
                processStateForSnackbar(state)
            }
    }

    private fun processStateForSnackbar(state: IPProtectionState) {
        if (state.eligibilityStatus == EligibilityStatus.Eligible && state.proxyStatus == Authorized.DataLimitReached) {
            val resetTime = state.usageQuota?.resetTime

            if (resetTime != null && repository.lastDataLimitNoticeCycleResetTime != resetTime) {
                repository.lastDataLimitNoticeCycleResetTime = resetTime

                Vpn.bandwidthLimitError.record()
                appStore.dispatch(
                    action =
                        AppAction.IPProtectionSnackbarAction.ShowActionSettingsSnackbar(
                            title = errorMessages.dataLimitReached
                        )
                )
            }
        }
    }
}
