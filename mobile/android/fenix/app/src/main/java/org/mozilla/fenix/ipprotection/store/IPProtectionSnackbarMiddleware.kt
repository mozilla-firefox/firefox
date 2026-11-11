/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection.store

import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.IPProtectionSnackbarAction

/**
 * Localized snackbar messages dispatched by [IPProtectionSnackbarMiddleware].
 *
 * @property connectionError Message shown when activation fails with a connection error.
 * @property locationSelectionReset Message shown when the location selection defaults due to the previously selected
 *   location being no longer available.
 */
data class IPProtectionSnackbarMessages(
    val connectionError: String,
    val locationSelectionReset: String,
)

/**
 * A middleware for observing error states in [IPProtectionState] and notifying the Snackbar for user-facing messaging.
 *
 * This differs from the [IPProtectionInfoPrompter] which is similar, but handles sticky snackbar messages.
 */
class IPProtectionSnackbarMiddleware(
    private val lazyAppStore: Lazy<AppStore>,
    private val messages: IPProtectionSnackbarMessages,
) : Middleware<IPProtectionState, IPProtectionAction> {
    override fun invoke(
        store: Store<IPProtectionState, IPProtectionAction>,
        next: (IPProtectionAction) -> Unit,
        action: IPProtectionAction,
    ) {
        next(action)
        when (action) {
            is IPProtectionAction.ActivationAction.ToggleFailed ->
                lazyAppStore.value.dispatch(IPProtectionSnackbarAction.ShowSnackbar(title = messages.connectionError))

            is IPProtectionAction.LocationAction.LocationReset ->
                lazyAppStore.value.dispatch(
                    IPProtectionSnackbarAction.ShowActionSettingsSnackbar(title = messages.locationSelectionReset)
                )

            else -> {
                // no-op
            }
        }
    }
}
