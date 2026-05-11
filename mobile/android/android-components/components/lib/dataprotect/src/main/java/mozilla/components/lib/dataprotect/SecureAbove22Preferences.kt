/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.dataprotect

import android.content.Context
import android.content.Context.MODE_PRIVATE
import android.content.SharedPreferences
import android.util.Base64
import androidx.core.content.edit
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.support.base.log.logger.Logger
import java.nio.charset.StandardCharsets
import java.security.GeneralSecurityException

private interface KeyValuePreferences {
    /**
     * Retrieves all key/value pairs present in the store.
     *
     * @return A [Map] containing all key/value pairs present in the store.
     */
    fun all(): Map<String, String>

    /**
     * Retrieves a stored [key]. See [putString] for storing a [key].
     *
     * @param key A key name.
     * @return An optional [String] if [key] is present in the store.
     */
    fun getString(key: String): String?

    /**
     * Stores [value] under [key]. Retrieve it using [getString].
     *
     * @param key A key name.
     * @param value A value for [key].
     */
    fun putString(key: String, value: String)

    /**
     * Removes key/value pair from storage for the provided [key].
     */
    fun remove(key: String)

    /**
     * Clears all key/value pairs from the storage.
     */
    fun clear()
}

/**
 * A wrapper around [SharedPreferences] which encrypts contents on supported API versions (23+).
 * Otherwise, this simply delegates to [SharedPreferences].
 *
 * In rare circumstances (such as APK signing key rotation) a master key which protects this storage may be lost,
 * in which case previously stored values will be lost as well. Applications are encouraged to instrument such events.
 *
 * @param context A [Context], used for accessing [SharedPreferences].
 * @param name A name for this storage, used for isolating different instances of [SecureAbove22Preferences].
 * @param forceInsecure A flag indicating whether to force plaintext storage. If set to `true`,
 * [InsecurePreferencesImpl21] will be used as a storage layer
 */
class SecureAbove22Preferences(
    context: Context,
    name: String,
    forceInsecure: Boolean = false,
    crashReporting: CrashReporting? = null,
) : KeyValuePreferences {
    private val impl = if (!forceInsecure) {
        SecurePreferencesImpl23(context, name, crashReporting = crashReporting)
    } else {
        InsecurePreferencesImpl21(context, name)
    }

    override fun all(): Map<String, String> = impl.all()

    override fun getString(key: String) = impl.getString(key)

    override fun putString(key: String, value: String) = impl.putString(key, value)

    override fun remove(key: String) = impl.remove(key)

    override fun clear() = impl.clear()
}

/**
 * A simple [KeyValuePreferences] implementation which entirely delegates to [SharedPreferences] and doesn't perform any
 * encryption/decryption.
 */
@SuppressWarnings("TooGenericExceptionCaught")
private class InsecurePreferencesImpl21(
    context: Context,
    name: String,
    migrateFromSecureStorage: Boolean = true,
) : KeyValuePreferences {
    companion object {
        private const val SUFFIX = "_kp_pre_m"
    }

    internal val logger = Logger("mozac/InsecurePreferencesImpl21")

    private val prefs = context.getSharedPreferences("$name$SUFFIX", MODE_PRIVATE)

    init {
        // Check if we have any encrypted values stored on disk.
        if (migrateFromSecureStorage && prefs.all.isEmpty()) {
            val secureStorage = SecurePreferencesImpl23(context, name, false)
            // Copy over any old values.
            try {
                secureStorage.all().forEach {
                    putString(it.key, it.value)
                }
            } catch (e: Exception) {
                // Certain devices crash on various Keystore exceptions. While trying to migrate
                // to use the plaintext storage we don't want to crash if we can't access secure
                // storage, and just catch the errors.
                logger.error("Migrating from secure storage failed", e)
            }
            // Erase old storage.
            secureStorage.clear()
        }
    }

    override fun all(): Map<String, String> {
        return prefs.all.mapNotNull {
            if (it.value is String) {
                it.key to it.value as String
            } else {
                null
            }
        }.toMap()
    }

    override fun getString(key: String) = prefs.getString(key, null)

    override fun putString(key: String, value: String) {
        prefs.edit { putString(key, value) }
    }

    override fun remove(key: String) {
        prefs.edit { remove(key) }
    }

    override fun clear() {
        prefs.edit { clear() }
    }
}

/**
 * A [KeyValuePreferences] which is backed by [SharedPreferences] and performs encryption/decryption of values.
 */
private class SecurePreferencesImpl23(
    context: Context,
    name: String,
    migrateFromPlaintextStorage: Boolean = true,
    crashReporting: CrashReporting? = null,
) : KeyValuePreferences {
    companion object {
        private const val SUFFIX = "_kp_post_m"
        private const val BASE_64_FLAGS = Base64.URL_SAFE or Base64.NO_PADDING
    }

    private val logger = Logger("SecurePreferencesImpl23")
    private val prefs = context.getSharedPreferences("$name$SUFFIX", MODE_PRIVATE)
    private val keystore by lazy { Keystore(context.packageName, crashReporting = crashReporting) }

    init {
        if (migrateFromPlaintextStorage && prefs.all.isEmpty()) {
            // Check if we have any plaintext values stored on disk. That indicates that we've hit
            // an API upgrade situation. We just went from pre-M to post-M. Since we already have
            // the plaintext keys, we can transparently migrate them to use the encrypted storage layer.
            val insecureStorage = InsecurePreferencesImpl21(context, name, false)
            // Copy over any old values.
            insecureStorage.all().forEach {
                putString(it.key, it.value)
            }
            // Erase old storage.
            insecureStorage.clear()
        }
    }

    override fun all(): Map<String, String> {
        return prefs.all.keys.mapNotNull { key ->
            getString(key)?.let { value ->
                key to value
            }
        }.toMap()
    }

    override fun getString(key: String): String? {
        // The fact that we're possibly generating a managed key here implies that this key could be lost after being
        // for some reason. One possible reason for a key to be lost is rotating signing keys for the APK.
        // Applications are encouraged to instrument such events.
        generateManagedKeyIfNecessary()

        if (!prefs.contains(key)) {
            return null
        }

        val value = prefs.getString(key, "")
        val encrypted = Base64.decode(value, BASE_64_FLAGS)

        return try {
            String(keystore.decryptBytes(encrypted), StandardCharsets.UTF_8)
        } catch (error: IllegalArgumentException) {
            logger.error("IllegalArgumentException exception: ", error)
            null
        } catch (error: GeneralSecurityException) {
            logger.error("Decrypt exception: ", error)
            null
        }
    }

    override fun putString(key: String, value: String) {
        generateManagedKeyIfNecessary()
        val encrypted = keystore.encryptBytes(value.toByteArray(StandardCharsets.UTF_8))
        val data = Base64.encodeToString(encrypted, BASE_64_FLAGS)

        prefs.edit { putString(key, data) }
    }

    override fun remove(key: String) {
        prefs.edit { remove(key) }
    }

    override fun clear() {
        prefs.edit { clear() }
    }

    /**
     * Generates a "managed key" - a key used to encrypt data stored by this class. This key is "managed" by [Keystore],
     * which stores it in system's secure storage layer exposed via [AndroidKeyStore].
     */
    private fun generateManagedKeyIfNecessary() {
        // Do we need to check this on every access, or just during instantiation? Is the overhead here worth it?
        if (!keystore.available()) {
            keystore.generateKey()
        }
    }
}
