/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.dataprotect

import android.os.Build
import android.security.KeyStoreException
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import androidx.annotation.RequiresApi
import mozilla.components.concept.base.crash.Breadcrumb
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.support.base.log.logger.Logger
import java.security.GeneralSecurityException
import java.security.InvalidKeyException
import java.security.Key
import java.security.KeyStore
import java.security.UnrecoverableKeyException
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec
import android.security.KeyStoreException as AndroidKeyStoreException

private const val KEYSTORE_TYPE = "AndroidKeyStore"
private const val ENCRYPTED_VERSION = 0x02

internal const val CIPHER_ALG = KeyProperties.KEY_ALGORITHM_AES

internal const val CIPHER_MOD = KeyProperties.BLOCK_MODE_GCM

internal const val CIPHER_PAD = KeyProperties.ENCRYPTION_PADDING_NONE
internal const val CIPHER_KEY_LEN = 256
internal const val CIPHER_TAG_LEN = 128

internal const val CIPHER_SPEC = "$CIPHER_ALG/$CIPHER_MOD/$CIPHER_PAD"

internal const val CIPHER_NONCE_LEN = 12

// Apps targeting API 37+: limit is 50,000 keys → error code ERROR_TOO_MANY_KEYS.
// Apps not targeting API 37+: limit is 200,000 keys → error code ERROR_INCORRECT_USAGE.
private const val KEY_LIMIT_NON_TARGETING_API37 = 200_000

/**
 * Wraps the critical functions around a Java KeyStore to better facilitate testing
 * and instrumenting.
 *
 */
open class KeyStoreWrapper(
    private val crashReporting: CrashReporting? = null,
) {
    private var keystore: KeyStore? = null
    private val logger = Logger("KeyStoreWrapper")

    /**
     * Retrieves the underlying KeyStore, loading it if necessary.
     */
    fun getKeyStore(): KeyStore {
        var ks = keystore
        if (ks == null) {
            ks = loadKeyStore()
            keystore = ks
        }

        return ks
    }

    /**
     * Retrieves the SecretKey for the given label.
     *
     * This method queries for a SecretKey with the given label and no passphrase.
     *
     * Subclasses override this method if additional properties are needed
     * to retrieve the key.
     *
     * @param label The label to query
     * @return The key for the given label, or `null` if not present
     * @throws InvalidKeyException If there is a Key but it is not a SecretKey
     * @throws NoSuchAlgorithmException If the recovery algorithm is not supported
     */
    open fun getKeyFor(label: String): Key? = try {
        loadKeyStore().getKey(label, null)
    } catch (e: UnrecoverableKeyException) {
        logger.error("Failed to get key", e)
        null
    }

    /**
     * Creates a SecretKey for the given label.
     *
     * This method generates a SecretKey pre-bound to the `AndroidKeyStore` and configured
     * with the strongest "algorithm/blockmode/padding" (and key size) available.
     *
     * Subclasses override this method to properly associate the generated key with
     * the given label in the underlying KeyStore.
     *
     * @param label The label to associate with the created key
     * @return The newly-generated key for `label`
     * @throws NoSuchAlgorithmException If the cipher algorithm is not supported
     */
    open fun makeKeyFor(label: String): SecretKey {
        val spec = KeyGenParameterSpec.Builder(
            label,
            KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT,
        )
            .setKeySize(CIPHER_KEY_LEN)
            .setBlockModes(CIPHER_MOD)
            .setEncryptionPaddings(CIPHER_PAD)
            .build()
        val gen = KeyGenerator.getInstance(CIPHER_ALG, KEYSTORE_TYPE)
        gen.init(spec)
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.CINNAMON_BUN) {
            generateKeyHandlingLimit(gen)
        } else {
            gen.generateKey()
        }
    }

    @RequiresApi(Build.VERSION_CODES.CINNAMON_BUN)
    private fun generateKeyHandlingLimit(gen: KeyGenerator): SecretKey {
        return try {
            gen.generateKey()
        } catch (e: AndroidKeyStoreException) {
            val keyCount = runCatching { getKeyStore().size() }.getOrNull()
            val context = mapOf("keyCount" to "$keyCount", "errorCode" to "${e.numericErrorCode}")
            val message = when (e.numericErrorCode) {
                AndroidKeyStoreException.ERROR_TOO_MANY_KEYS ->
                    "Android Keystore key limit exceeded (current key count: $keyCount)"

                AndroidKeyStoreException.ERROR_INCORRECT_USAGE ->
                    if (keyCount != null && keyCount >= KEY_LIMIT_NON_TARGETING_API37) {
                        "Android Keystore key limit exceeded (current key count: $keyCount)"
                    } else {
                        "Android Keystore key generation failed with ERROR_INCORRECT_USAGE " +
                            "(current key count: $keyCount)"
                    }

                else ->
                    "Android Keystore key generation failed (error code: ${e.numericErrorCode})"
            }
            logger.error(message, e)
            crashReporting?.recordCrashBreadcrumb(
                Breadcrumb(
                    message = message,
                    data = context,
                    category = "KeyStoreWrapper",
                    level = Breadcrumb.Level.ERROR,
                ),
            )
            throw e
        }
    }

    /**
     * Deletes a key with the given label.
     *
     * @param label The label of the associated key to delete
     * @throws KeyStoreException If there is no key for `label`
     */
    fun removeKeyFor(label: String) {
        getKeyStore().deleteEntry(label)
    }

    /**
     * Creates and initializes the KeyStore in use.
     *
     * This method loads a`"AndroidKeyStore"` type KeyStore.
     *
     * Subclasses override this to load a KeyStore appropriate to the testing environment.
     *
     * @return The KeyStore, already initialized
     * @throws KeyStoreException if the type of store is not supported
     */
    open fun loadKeyStore(): KeyStore {
        val ks = KeyStore.getInstance(KEYSTORE_TYPE)
        ks.load(null)
        return ks
    }
}

/**
 * Manages data protection using a system-isolated cryptographic key.
 *
 * This class provides for both:
 * * management for a specific crypto graphic key (identified by a string label)
 * * protection (encryption/decryption) of data using the managed key
 *
 * The specific cryptographic properties are pre-chosen to be the following:
 * * Algorithm is "AES/GCM/NoPadding"
 * * Key size is 256 bits
 * * Tag size is 128 bits
 *
 * @property label The label the cryptographic key is identified as
 * @constructor Creates a new instance around a key identified by the given label
 *
 * Unless `manual` is `true`, the key is created if not already present in the
 * platform's key storage.
 */
open class Keystore(
    val label: String,
    manual: Boolean = false,
    crashReporting: CrashReporting? = null,
    internal val wrapper: KeyStoreWrapper = KeyStoreWrapper(crashReporting),
) {
    init {
        if (!manual and !available()) {
            generateKey()
        }
    }

    private fun getKey(): SecretKey? =
        wrapper.getKeyFor(label) as? SecretKey?

    /**
     * Determines if the managed key is available for use.  Consumers can use this to
     * determine if the key was somehow lost and should treat any previously-protected
     * data as invalid.
     *
     * @return `true` if the managed key exists and ready for use.
     */
    fun available(): Boolean = (getKey() != null)

    /**
     * Generates the managed key if it does not already exist.
     *
     * @return `true` if a new key was generated; `false` if the key already exists and can
     * be used.
     * @throws GeneralSecurityException If the key could not be created
     */
    @Throws(GeneralSecurityException::class)
    fun generateKey(): Boolean {
        val key = wrapper.getKeyFor(label)
        if (key != null) {
            when (key) {
                is SecretKey -> return false
                else -> throw InvalidKeyException("unsupported key type")
            }
        }

        wrapper.makeKeyFor(label)

        return true
    }

    /**
     *  Deletes the managed key.
     *
     *  **NOTE:** Once this method returns, any data protected with the (formerly) managed
     *  key cannot be decrypted and therefore is inaccessble.
     */
    fun deleteKey() {
        val key = wrapper.getKeyFor(label)
        if (key != null) {
            wrapper.removeKeyFor(label)
        }
    }

    /**
     * Encrypts data using the managed key.
     *
     * The output of this method includes the input factors (i.e., initialization vector),
     * ciphertext, and authentication tag as a single byte string; this output can be passed
     * directly to [decryptBytes].
     *
     * @param plain The "plaintext" data to encrypt
     * @return The encrypted data to be stored
     * @throws GeneralSecurityException If the data could not be encrypted
     */
    @Throws(GeneralSecurityException::class)
    open fun encryptBytes(plain: ByteArray): ByteArray {
        // 5116-style interface  = [ inputs || ciphertext || atag ]
        // - inputs = [ version = 0x02 || cipher.iv (always 12 bytes) ]
        // - cipher.doFinal() provides [ ciphertext || atag ]
        // Cipher operations are not thread-safe so we synchronize over them through doFinal to
        // prevent crashes with quickly repeated encrypt/decrypt operations
        // https://github.com/mozilla-mobile/android-components/issues/5342
        synchronized(this) {
            val cipher = createEncryptCipher()
            val cdata = cipher.doFinal(plain)
            val nonce = cipher.iv

            return byteArrayOf(ENCRYPTED_VERSION.toByte()) + nonce + cdata
        }
    }

    /**
     * Decrypts data using the managed key.
     *
     * The input of this method is expected to include input factors (i.e., initialization
     * vector), ciphertext, and authentication tag as a single byte string; it is the direct
     * output from [encryptBytes].
     *
     * @param encrypted The encrypted data to decrypt
     * @return The decrypted "plaintext" data
     * @throws KeystoreException If the data could not be decrypted
     */
    @Throws(KeystoreException::class)
    open fun decryptBytes(encrypted: ByteArray): ByteArray {
        val version = encrypted[0].toInt()
        if (version != ENCRYPTED_VERSION) {
            throw KeystoreException("unsupported encrypted version: $version")
        }

        // Cipher operations are not thread-safe so we synchronize over them through doFinal to
        // prevent crashes with quickly repeated encrypt/decrypt operations
        // https://github.com/mozilla-mobile/android-components/issues/5342
        synchronized(this) {
            val iv = encrypted.sliceArray(1..CIPHER_NONCE_LEN)
            val cdata = encrypted.sliceArray((CIPHER_NONCE_LEN + 1)..encrypted.size - 1)
            val cipher = createDecryptCipher(iv)
            return cipher.doFinal(cdata)
        }
    }

    /**
     * Create a cipher initialized for encrypting data with the managed key.
     *
     * This "low-level" method is useful when a cryptographic context is needed to integrate with
     * other APIs, such as the `FingerprintManager`.
     *
     * **NOTE:** The caller is responsible for associating certain encryption factors, such as
     * the initialization vector and/or additional authentication data (AAD), with the resulting
     * ciphertext or decryption will fail.
     *
     * @return The [Cipher], initialized and ready to encrypt data with.
     * @throws GeneralSecurityException If the Cipher could not be created and initialized
     */
    @Throws(GeneralSecurityException::class)
    open fun createEncryptCipher(): Cipher {
        val key = getKey() ?: throw InvalidKeyException("unknown label: $label")
        val cipher = Cipher.getInstance(CIPHER_SPEC)
        cipher.init(Cipher.ENCRYPT_MODE, key)

        return cipher
    }

    /**
     * Create a cipher initialized for decrypting data with the managed key.
     *
     * This "low-level" method is useful when a cryptographic context is needed to integrate with
     * other APIs, such as the `FingerprintManager`.
     *
     * **NOTE:** The caller is responsible for associating certain encryption factors, such as
     * the initialization vector and/or additional authentication data (AAD), with the stored
     * ciphertext or decryption will fail.
     *
     * @param iv The initialization vector/nonce to decrypt with
     * @return The [Cipher], initialized and ready to decrypt data with.
     * @throws GeneralSecurityException If the cipher could not be created and initialized
     */
    @Throws(GeneralSecurityException::class)
    open fun createDecryptCipher(iv: ByteArray): Cipher {
        val key = getKey() ?: throw InvalidKeyException("unknown label: $label")
        val cipher = Cipher.getInstance(CIPHER_SPEC)
        cipher.init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(CIPHER_TAG_LEN, iv))

        return cipher
    }
}
