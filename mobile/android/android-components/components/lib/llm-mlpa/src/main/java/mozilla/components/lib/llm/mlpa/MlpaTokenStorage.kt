/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.mlpa

import android.content.Context
import android.content.Context.MODE_PRIVATE
import android.content.SharedPreferences
import androidx.core.content.edit
import mozilla.components.lib.llm.mlpa.service.AuthorizationToken
import kotlin.time.Clock
import kotlin.time.Duration
import kotlin.time.Instant

/**
 * Manages persistent storage for [AuthorizationToken]s used by the MLPA service.
 */
interface MlpaTokenStorage {
    /**
     * Returns the stored [AuthorizationToken], or null if no token is available or the token
     * has expired.
     */
    suspend fun getToken(): AuthorizationToken.Integrity?

    /**
     * Persists [token] alongside the current time so expiry can be checked on retrieval.
     *
     * @param token The token to store.
     * @param expiresIn How long the token is valid from the time this method is called.
     */
    suspend fun setToken(token: AuthorizationToken.Integrity, expiresIn: Duration)

    /**
     * Clears any persisted token from storage.
     */
    suspend fun clear()

    companion object {
        /**
         * Creates a static [MlpaTokenStorage]. This will return whatever is passed in at the time of
         * construction.
         *
         * @param token an [AuthorizationToken.Integrity]
         */
        fun static(token: AuthorizationToken.Integrity? = null) = object : MlpaTokenStorage {
            override suspend fun getToken() = token

            override suspend fun setToken(
                token: AuthorizationToken.Integrity,
                expiresIn: Duration,
            ) {}

            override suspend fun clear() {}
        }

        /**
         * Creates an implementation of [MlpaTokenStorage] that is backed by [SharedPreferences].
         *
         * @param context required to create [SharedPreferences].
         * @return An instance of [MlpaTokenStorage].
         */
        fun sharedPrefs(
            context: Context,
        ): MlpaTokenStorage {
            val prefs = context.getSharedPreferences("mlpa_token_storage", MODE_PRIVATE)
            return SharedPreferencesBackedMlpaStorage(prefs)
        }
    }
}

internal class SharedPreferencesBackedMlpaStorage(
    val prefs: SharedPreferences,
    val clock: Clock = Clock.System,
) : MlpaTokenStorage {
    override suspend fun getToken(): AuthorizationToken.Integrity? {
        return prefs.token?.takeIf { prefs.expiresAt > clock.now() }
    }

    override suspend fun setToken(
        token: AuthorizationToken.Integrity,
        expiresIn: Duration,
    ) {
        prefs.edit {
            putString(TOKEN_KEY, token.value)
            putLong(EXPIRES_AT_KEY, (clock.now() + expiresIn).toEpochMilliseconds())
        }
    }

    override suspend fun clear() {
        prefs.edit {
            clear()
        }
    }

    private val SharedPreferences.token get() = getString(TOKEN_KEY, null)?.let {
        AuthorizationToken.Integrity(it)
    }

    private val SharedPreferences.expiresAt get() = Instant.fromEpochMilliseconds(getLong(EXPIRES_AT_KEY, 0L))

    companion object {
        const val TOKEN_KEY = "MLPA_TOKEN"
        const val EXPIRES_AT_KEY = "MLPA_EXPIRES_AT"
    }
}
