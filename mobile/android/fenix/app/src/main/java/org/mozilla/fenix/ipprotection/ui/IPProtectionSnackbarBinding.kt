/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection.ui

import android.content.Context
import androidx.annotation.StringRes
import androidx.navigation.NavController
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import mozilla.components.lib.state.helpers.AbstractBinding
import mozilla.components.ui.widgets.SnackbarDelegate
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.components.appstate.AppAction.SnackbarAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.snackbar.FenixSnackbarDelegate
import org.mozilla.fenix.snackbar.SnackbarBinding
import org.mozilla.fenix.utils.getSnackbarTimeout

/**
 * A binding for observing the [SnackbarState] for IP Protection related states in the [AppStore].
 *
 * Unlike [SnackbarBinding], this binding handles specifically IP Protection related snackbar states. So that, in cases
 * where we could have multiple snackbar bindings (e.g., a home fragment + a menu dialog fragment active together), we
 * could have granular control over which types of snackbars the menu should consume. (The consumption could be
 * potentially a problem; for example, adding a bookmark through the menu shows a snackbar and dismisses the menu - if
 * the menu consumed the bookmark and immediately dismissed itself, the snackbar would not get visible for the user).
 *
 * @param appStore The [AppStore] used to observe the [SnackbarState].
 * @param context The Android [Context] used for system interactions and accessing resources.
 * @param navController [NavController] used for navigation.
 * @param snackbarDelegate The [SnackbarDelegate] used to display a snackbar.
 * @param mainDispatcher The [CoroutineDispatcher] on which the state observation and updates will occur.
 */
class IPProtectionSnackbarBinding(
    private val appStore: AppStore,
    private val context: Context,
    private val navController: NavController,
    private val snackbarDelegate: FenixSnackbarDelegate,
    mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : AbstractBinding<AppState>(appStore, mainDispatcher) {

    override suspend fun onState(flow: Flow<AppState>) {
        flow
            .map { state -> state.snackbarState }
            .distinctUntilChanged()
            .collect { state ->
                when (state) {
                    is SnackbarState.IPProtectionShowSnackbar -> showIPProtectionSnackBar(state.title)
                    is SnackbarState.IPProtectionShowActionSettingsSnackbar ->
                        showActionSettingsSnackbar(state.titleResId, state.arguments)
                    else -> {
                        // no-op, this binding is only concerned with ip protection related snackbars
                    }
                }
            }
    }

    private fun showIPProtectionSnackBar(title: String) {
        snackbarDelegate.show(
            text = title,
            duration = context.components.settings.getSnackbarTimeout().value.toInt(),
        )

        appStore.dispatch(SnackbarAction.SnackbarShown)
    }

    @Suppress("SpreadOperator") // getString(... *arguments..)
    private fun showActionSettingsSnackbar(@StringRes titleResId: Int, arguments: List<Any>) {
        snackbarDelegate.show(
            text = context.getString(titleResId, *arguments.toTypedArray()),
            duration = context.components.settings.getSnackbarTimeout(hasAction = true).value.toInt(),
            action = context.getString(R.string.ip_protection_open_settings_snackbar_action),
        ) {
            navController.navigate(
                BrowserFragmentDirections.actionGlobalIpProtectionFragment(
                    entrypoint = FenixFxAEntryPoint.IPProtectionSettings
                )
            )
        }

        appStore.dispatch(SnackbarAction.SnackbarShown)
    }
}
