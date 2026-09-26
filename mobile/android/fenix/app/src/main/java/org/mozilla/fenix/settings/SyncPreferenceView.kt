/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import androidx.lifecycle.LifecycleOwner
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.launch
import mozilla.components.concept.sync.AccountObserver
import mozilla.components.concept.sync.AuthType
import mozilla.components.concept.sync.OAuthAccount
import mozilla.components.concept.sync.SyncEngine
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.service.fxa.manager.SyncEnginesStorage

/**
 * A view to help manage the sync preference in the "Passwords" and "Payment Methods" settings. The provided
 * [syncPreference] is used to navigate to the different fragments that manages the sync account authentication. A
 * toggle will be also added depending on the sync account status.
 *
 * @param syncPreference The sync [SyncPreference] to update and handle navigation.
 * @param lifecycleOwner View lifecycle owner used to determine when to cancel UI jobs.
 * @param coroutineScope [CoroutineScope] used to launch coroutines for engine state changes.
 * @param accountManager An instance of [FxaAccountManager].
 * @param syncEngine The sync engine that will be used for the sync status lookup.
 * @param loggedOffTitle Text label for the setting when user is not logged in.
 * @param loggedInTitle Text label for the setting when user is logged in.
 * @param onSyncSignInClicked A callback executed when the sync sign in [syncPreference] is clicked.
 * @param onReconnectClicked A callback executed when the [syncPreference] is clicked with a preference status of
 *   "Reconnect".
 */
class SyncPreferenceView(
    private val syncPreference: SyncPreference,
    private val lifecycleOwner: LifecycleOwner,
    private val coroutineScope: CoroutineScope,
    private val accountManager: FxaAccountManager,
    private val syncEngine: SyncEngine,
    private val loggedOffTitle: String,
    private val loggedInTitle: String,
    private val onSyncSignInClicked: () -> Unit = {},
    private val onReconnectClicked: () -> Unit = {},
) {

    init {
        accountManager.register(
            object : AccountObserver {
                override fun onAuthenticated(account: OAuthAccount, authType: AuthType) {
                    MainScope().launch { updateSyncPreferenceStatus() }
                }

                override fun onLoggedOut() {
                    MainScope().launch { updateSyncPreferenceNeedsLogin() }
                }

                override fun onAuthenticationProblems() {
                    MainScope().launch { updateSyncPreferenceNeedsReauth() }
                }
            },
            owner = lifecycleOwner,
        )

        val accountExists = accountManager.authenticatedAccount() != null
        val needsReauth = accountManager.accountNeedsReauth()

        when {
            needsReauth -> updateSyncPreferenceNeedsReauth()
            accountExists -> updateSyncPreferenceStatus()
            !accountExists -> updateSyncPreferenceNeedsLogin()
        }
    }

    /** Shows a switch toggle for the sync preference when the user is logged in. */
    private fun updateSyncPreferenceStatus() {
        syncPreference.apply {
            isSwitchWidgetVisible = true

            val syncEnginesStatus = SyncEnginesStorage(context).getStatus()
            val syncStatus = syncEnginesStatus.getOrElse(syncEngine) { false }

            title = loggedInTitle
            isChecked = syncStatus

            setOnPreferenceChangeListener { _, newValue ->
                coroutineScope.launch {
                    accountManager.setEngineEnabled(syncEngine, newValue as Boolean)
                }
                setSwitchCheckedState(newValue as Boolean)
                true
            }
        }
    }

    /** Display that the user can sync across devices when the user is logged off. */
    private fun updateSyncPreferenceNeedsLogin() {
        syncPreference.apply {
            isSwitchWidgetVisible = false

            title = loggedOffTitle

            setOnPreferenceChangeListener { _, _ ->
                onSyncSignInClicked()
                false
            }
        }
    }

    /** Displays the logged off title to prompt the user to to re-authenticate their sync account. */
    private fun updateSyncPreferenceNeedsReauth() {
        syncPreference.apply {
            isSwitchWidgetVisible = false

            title = loggedOffTitle

            setOnPreferenceChangeListener { _, _ ->
                onReconnectClicked()
                false
            }
        }
    }
}
