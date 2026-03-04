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
import mozilla.components.support.ktx.kotlin.toHexString
import java.security.MessageDigest
import java.util.UUID

/**
 * Interface for providing a hashing function to [ClientUUID].
 */
fun interface Hasher {
    /**
     * Hash a value.
     * @param value to be hashed
     * @return the hashed value.
     */
    fun hash(value: String): String

    companion object {
        /**
         * A [Hasher] implementation that hashes using SHA256.
         */
        val sha256 get() = Hasher { value ->
            MessageDigest.getInstance("SHA256")
                .digest(value.toByteArray())
                .toHexString()
        }
    }
}

/**
 * Generates and persists a stable per-install UUID, used to identify this client
 * consistently across [UserIdProvider] and [RequestHashProvider] consumers.
 */
interface ClientUUID : UserIdProvider, RequestHashProvider {
    companion object {
        /**
         * Convenience initializer that creates a [SharedPreferences] to be used by [ClientUUID].
         *
         * @param context the application context.
         * @return an instance of [ClientUUID]
         */
        fun build(context: Context): ClientUUID {
            return PrefsBackedClientUUID({
                context.getSharedPreferences("client_uuid", Context.MODE_PRIVATE)
            })
        }
    }
}

internal class PrefsBackedClientUUID(
    private val getPrefs: () -> SharedPreferences,
    private val generateUUID: () -> String = { UUID.randomUUID().toString() },
    private val hasher: Hasher = Hasher.sha256,
) : ClientUUID {
    private val uuid: String by lazy {
        getPrefs().let { prefs ->
            prefs.getString(KEY, null) ?: generateUUID().also {
                prefs.edit { putString(KEY, it) }
            }
        }
    }

    override fun getUserId() = UserId(uuid)

    override fun generateHash() = hasher.hash(uuid)

    companion object {
        private const val KEY = "uuid"
    }
}
