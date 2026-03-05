/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import android.content.Context
import android.content.Context.MODE_PRIVATE
import android.content.SharedPreferences
import androidx.core.content.edit

/**
 * Defines the contract for persisting and retrieving user settings related to
 * the summarization feature.
 *
 * Implementations are responsible for the underlying storage mechanism, such as
 * [SharedPreferences] or a database.
 *
 * @see [SummarizationSettings.Companion.inMemory] for a simple in-memory implementation
 * suitable for testing or previews.
 */
interface SummarizationSettings {
    /**
     * Returns whether the user has consented to the shake gesture interaction.
     *
     * @return `true` if the user has previously accepted the shake consent prompt,
     * `false` otherwise.
     */
    suspend fun hasConsentedToShake(): Boolean

    /**
     * Persists the user's consent status for the shake gesture interaction.
     *
     * @param newValue `true` to indicate the user has consented, `false` to revoke consent.
     */
    suspend fun setHasConsentedToShake(newValue: Boolean)

    companion object {
        /**
         * Creates a simple in-memory implementation of [SummarizationSettings].
         *
         * This implementation does not persist data across sessions and is intended
         * for use in tests, Compose previews, or other scenarios where a lightweight,
         * non-persistent implementation is needed.
         *
         * @param hasConsentedToShakeInitial The initial value for the shake consent setting.
         * Defaults to `false`.
         * @return An in-memory [SummarizationSettings] instance.
         */
        fun inMemory(
            hasConsentedToShakeInitial: Boolean = false,
        ) = object : SummarizationSettings {
            var hasConsentedToShake = hasConsentedToShakeInitial

            override suspend fun hasConsentedToShake() = hasConsentedToShake

            override suspend fun setHasConsentedToShake(newValue: Boolean) {
                hasConsentedToShake = newValue
            }
        }

        /**
         * Creates an implementation of [SummarizationSettings] that is backed by [SharedPreferences].
         *
         * @param context required to create [SharedPreferences].
         * @return An in-memory [SummarizationSettings] instance.
         */
        fun sharedPrefs(
            context: Context,
        ): SummarizationSettings {
            val prefs = context.getSharedPreferences("summarization_prefs", MODE_PRIVATE)
            return SharedPrefsBackedSummarizationSettings(prefs)
        }
    }
}

internal class SharedPrefsBackedSummarizationSettings(
    private val prefs: SharedPreferences,
) : SummarizationSettings {
    override suspend fun hasConsentedToShake(): Boolean {
        return prefs.getBoolean(HAS_CONSENT_TO_SHAKE, false)
    }

    override suspend fun setHasConsentedToShake(newValue: Boolean) {
        prefs.edit { putBoolean(HAS_CONSENT_TO_SHAKE, newValue) }
    }

    companion object {
        const val HAS_CONSENT_TO_SHAKE = "has_consent_to_shake"
    }
}
