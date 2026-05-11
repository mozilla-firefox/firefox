/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.ipprotection

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import mozilla.components.feature.ipprotection.Authorized
import mozilla.components.feature.ipprotection.EligibilityStatus
import mozilla.components.feature.ipprotection.IPProtectionState
import mozilla.components.feature.ipprotection.IPProtectionStore
import mozilla.components.lib.state.helpers.AbstractBinding
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction

/**
 * Error messages displayed to the user when IP protection operations encounter errors.
 *
 * @property connectionError Message shown when the proxy encounters a connection error.
 * @property dataLimitReached Message shown when the user has reached their monthly data limit.
 */
data class ErrorMessages(
    val connectionError: String,
    val dataLimitReached: String,
)

/**
 * Monitors [IPProtectionStore] state changes and displays informational snackbars to the user via [AppStore].
 *
 * @param store The IP protection store to observe for state changes.
 * @param appStore The app store used to dispatch snackbar actions.
 * @param errorMessages Localized error messages to display in snackbars.
 * @param mainDispatcher the dispatcher to run asynchronous work on.
 */
class IPProtectionInfoPrompter(
    store: IPProtectionStore,
    private val appStore: AppStore,
    private val errorMessages: ErrorMessages,
    mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : AbstractBinding<IPProtectionState>(store, mainDispatcher) {
    override suspend fun onState(flow: Flow<IPProtectionState>) {
        flow.distinctUntilChanged { old, new ->
            old.proxyStatus == new.proxyStatus && old.eligibilityStatus == new.eligibilityStatus
        }.collect { state ->
            processStateForSnackbar(state)
        }
    }

    private fun processStateForSnackbar(state: IPProtectionState) {
        if (state.eligibilityStatus == EligibilityStatus.Eligible) {
            when (state.proxyStatus) {
                Authorized.DataLimitReached -> {
                    appStore.dispatch(AppAction.SnackbarAction.ShowSnackbar(errorMessages.dataLimitReached))
                }
                Authorized.ConnectionError -> {
                    appStore.dispatch(AppAction.SnackbarAction.ShowSnackbar(errorMessages.connectionError))
                }
                else -> {
                    // no-op
                }
            }
        }
    }
}
