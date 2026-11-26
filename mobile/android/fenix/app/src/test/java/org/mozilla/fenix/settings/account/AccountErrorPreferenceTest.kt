/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.account

import android.content.Context
import android.view.LayoutInflater
import android.view.View
import android.widget.TextView
import androidx.core.content.ContextCompat
import androidx.preference.PreferenceViewHolder
import androidx.test.core.app.ApplicationProvider
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class AccountErrorPreferenceTest {
    private val context: Context = ApplicationProvider.getApplicationContext()

    @Test
    fun `binds sync error content and retries`() {
        var actionCalled = false
        val preference =
            createPreference(AccountErrorPreference.State.SyncError("Last sync: 10/20/2026")).apply {
                onCardClick = { actionCalled = true }
            }
        val view = bind(preference)

        assertEquals(
            context.getString(R.string.account_sync_error_title),
            view.findViewById<TextView>(R.id.account_error_title).text,
        )
        assertEquals(
            "Last sync: 10/20/2026",
            view.findViewById<TextView>(R.id.account_error_summary).text,
        )
        assertEquals(
            context.getString(R.string.account_sync_error_retry),
            view.findViewById<TextView>(R.id.account_error_action).text,
        )
        assertEquals(
            ContextCompat.getColor(context, R.color.fx_mobile_on_error_container),
            view.findViewById<TextView>(R.id.account_error_title).currentTextColor,
        )
        assertEquals(
            ContextCompat.getColor(context, R.color.fx_mobile_error_container),
            view.backgroundTintList?.defaultColor,
        )
        view.performClick()
        assertTrue(actionCalled)
    }

    @Test
    fun `binds account error content and sign in action`() {
        var actionCalled = false
        val preference =
            createPreference(AccountErrorPreference.State.AccountError).apply {
                onCardClick = { actionCalled = true }
            }
        val view = bind(preference)

        assertEquals(
            context.getString(R.string.account_error_title),
            view.findViewById<TextView>(R.id.account_error_title).text,
        )
        assertEquals(View.GONE, view.findViewById<TextView>(R.id.account_error_summary).visibility)
        assertEquals(
            context.getString(R.string.preferences_sign_in),
            view.findViewById<TextView>(R.id.account_error_action).text,
        )
        assertEquals(
            ContextCompat.getColor(context, R.color.fx_mobile_on_warning_container),
            view.findViewById<TextView>(R.id.account_error_title).currentTextColor,
        )
        assertEquals(
            ContextCompat.getColor(context, R.color.fx_mobile_warning_container),
            view.backgroundTintList?.defaultColor,
        )
        view.performClick()
        assertTrue(actionCalled)
    }

    private fun createPreference(state: AccountErrorPreference.State) =
        AccountErrorPreference(context).apply { this.state = state }

    private fun bind(preference: AccountErrorPreference) =
        LayoutInflater.from(context).inflate(R.layout.preference_account_error, null).also { view ->
            preference.onBindViewHolder(PreferenceViewHolder.createInstanceForTests(view))
        }
}
