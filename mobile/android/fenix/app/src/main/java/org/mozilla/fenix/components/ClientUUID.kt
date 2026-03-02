/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import android.content.Context
import android.content.SharedPreferences
import androidx.core.content.edit
import mozilla.components.lib.integrity.googleplay.RequestHashProvider
import mozilla.components.lib.llm.mlpa.UserIdProvider
import mozilla.components.lib.llm.mlpa.service.UserId
import java.util.UUID

/**
 * Generates and persists a stable per-install UUID, used to identify this client
 * consistently across [UserIdProvider] and [RequestHashProvider] consumers.
 */
class ClientUUID(
    private val prefs: SharedPreferences,
    private val generateUUID: () -> String = { UUID.randomUUID().toString() },
) : UserIdProvider, RequestHashProvider {
    private val uuid: String by lazy {
        prefs.getString(KEY, null) ?: generateUUID().also {
            prefs.edit { putString(KEY, it) }
        }
    }

    override fun getUserId() = UserId(uuid)

    override fun generateHash() = uuid

    companion object {
        private const val KEY = "uuid"

        /**
         * Convenience initializer that creates a [SharedPreferences] to be used by [ClientUUID].
         *
         * @param context the application context.
         * @return an instance of [ClientUUID]
         */
        fun build(context: Context): ClientUUID {
            val prefs = context.getSharedPreferences("client_uuid", Context.MODE_PRIVATE)
            return ClientUUID(prefs)
        }
    }
}
