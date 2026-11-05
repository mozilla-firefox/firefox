/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.account

import android.content.Context
import androidx.preference.Preference
import androidx.preference.PreferenceCategory
import androidx.preference.PreferenceFragmentCompat
import kotlinx.coroutines.CoroutineScope
import mozilla.components.concept.fetch.Client
import mozilla.components.concept.sync.Profile
import mozilla.components.service.fxa.manager.FxaAccountManager
import org.mozilla.fenix.R
import org.mozilla.fenix.settings.requirePreference

class AccountUiView(
    fragment: PreferenceFragmentCompat,
    private val scope: CoroutineScope,
    private val accountManager: FxaAccountManager,
    private val httpClient: Client,
) {

    private val preferenceSignIn = fragment.requirePreference<Preference>(R.string.pref_key_sign_in)
    private val preferenceFirefoxAccount = fragment.requirePreference<AccountPreference>(R.string.pref_key_account)
    private val preferenceFirefoxAccountAuthError =
        fragment.requirePreference<AccountAuthErrorPreference>(R.string.pref_key_account_auth_error)
    private val accountPreferenceCategory =
        fragment.requirePreference<PreferenceCategory>(R.string.pref_key_account_category)
    private val accountPreferenceUpdater =
        AccountPreferenceUpdater(
            preference = preferenceFirefoxAccount,
            scope = scope,
            httpClient = httpClient,
        )

    /**
     * Updates the UI to reflect current account state. Possible conditions are logged-in without problems, logged-out,
     * and logged-in but needs to re-authenticate.
     */
    fun updateAccountUIState(context: Context, profile: Profile?) {
        val account = accountManager.authenticatedAccount()

        // Signed-in, no problems.
        if (account != null && !accountManager.accountNeedsReauth()) {
            preferenceSignIn.isVisible = false

            accountPreferenceUpdater.update(context, profile)

            preferenceSignIn.onPreferenceClickListener = null
            preferenceFirefoxAccountAuthError.isVisible = false
            preferenceFirefoxAccount.isVisible = true
            accountPreferenceCategory.isVisible = true

            // Signed-in, need to re-authenticate.
        } else if (account != null && accountManager.accountNeedsReauth()) {
            preferenceFirefoxAccount.isVisible = false
            preferenceFirefoxAccountAuthError.isVisible = true
            accountPreferenceCategory.isVisible = true

            preferenceSignIn.isVisible = false
            preferenceSignIn.onPreferenceClickListener = null

            preferenceFirefoxAccountAuthError.email = profile?.email

            // Signed-out.
        } else {
            preferenceSignIn.isVisible = true
            preferenceFirefoxAccount.isVisible = false
            preferenceFirefoxAccountAuthError.isVisible = false
            accountPreferenceCategory.isVisible = false
        }
    }

    /** Cancel any running coroutine jobs for loading account images. */
    fun cancel() {
        accountPreferenceUpdater.cancel()
    }
}
