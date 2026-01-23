/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.storage

import android.annotation.SuppressLint
import android.os.Parcelable
import androidx.annotation.VisibleForTesting
import kotlinx.parcelize.Parcelize
import mozilla.components.concept.storage.CreditCard.Companion.ELLIPSES_END
import mozilla.components.concept.storage.CreditCard.Companion.ELLIPSES_START
import mozilla.components.concept.storage.CreditCard.Companion.ELLIPSIS
import mozilla.components.support.ktx.kotlin.last4Digits
import java.text.SimpleDateFormat
import java.util.Calendar
import java.util.Locale

/**
 * An interface which defines read/write methods for credit card and address data.
 */
interface CreditCardsAddressesStorage : Storage, StorageMaintenanceRegistry {

    /**
     * Inserts the provided credit card into the database, and returns
     * the newly added [CreditCard].
     *
     * @param creditCardFields A [NewCreditCardFields] record to add.
     * @return [CreditCard] for the added credit card.
     */
    suspend fun addCreditCard(creditCardFields: NewCreditCardFields): CreditCard

    /**
     * Updates the fields in the provided credit card.
     *
     * @param guid Unique identifier for the desired credit card.
     * @param creditCardFields A set of credit card fields, wrapped in [UpdatableCreditCardFields], to update.
     */
    suspend fun updateCreditCard(guid: String, creditCardFields: UpdatableCreditCardFields)

    /**
     * Retrieves the credit card from the underlying storage layer by its unique identifier.
     *
     * @param guid Unique identifier for the desired credit card.
     * @return [CreditCard] record if it exists or null otherwise.
     */
    suspend fun getCreditCard(guid: String): CreditCard?

    /**
     * Retrieves a list of all the credit cards.
     *
     * @return A list of all [CreditCard].
     */
    suspend fun getAllCreditCards(): List<CreditCard>

    /**
     * Deletes the credit card with the given [guid].
     *
     * @param guid Unique identifier for the desired credit card.
     * @return True if the deletion did anything, false otherwise.
     */
    suspend fun deleteCreditCard(guid: String): Boolean

    /**
     * Marks the credit card with the given [guid] as `in-use`.
     *
     * @param guid Unique identifier for the desired credit card.
     */
    suspend fun touchCreditCard(guid: String)

    /**
     * Inserts the provided address into the database, and returns
     * the newly added [Address].
     *
     * @param addressFields A [UpdatableAddressFields] record to add.
     * @return [Address] for the added address.
     */
    suspend fun addAddress(addressFields: UpdatableAddressFields): Address

    /**
     * Retrieves the address from the underlying storage layer by its unique identifier.
     *
     * @param guid Unique identifier for the desired address.
     * @return [Address] record if it exists or null otherwise.
     */
    suspend fun getAddress(guid: String): Address?

    /**
     * Retrieves a list of all the addresses.
     *
     * @return A list of all [Address].
     */
    suspend fun getAllAddresses(): List<Address>

    /**
     * Updates the fields in the provided address.
     *
     * @param guid Unique identifier for the desired address.
     * @param address The address fields to update.
     */
    suspend fun updateAddress(guid: String, address: UpdatableAddressFields)

    /**
     * Delete the address with the given [guid].
     *
     * @return True if the deletion did anything, false otherwise.
     */
    suspend fun deleteAddress(guid: String): Boolean

    /**
     * Marks the address with the given [guid] as `in-use`.
     *
     * @param guid Unique identifier for the desired address.
     */
    suspend fun touchAddress(guid: String)

    /**
     * Returns an instance of [CreditCardCrypto] that knows how to encrypt and decrypt credit card
     * numbers.
     *
     * @return [CreditCardCrypto] instance.
     */
    fun getCreditCardCrypto(): CreditCardCrypto

    /**
     * Removes any encrypted data from this storage. Useful after encountering key loss.
     */
    suspend fun scrubEncryptedData()

    override suspend fun runMaintenance(dbSizeLimit: UInt) {
        // Implemented by concrete implementation of `CreditCardsAddressesStorage`
    }

    override suspend fun warmUp() {
        // Implemented by concrete implementation of `CreditCardsAddressesStorage`
    }

    override fun registerStorageMaintenanceWorker() {
        // Implemented by concrete implementation of `CreditCardsAddressesStorage`
    }

    override fun unregisterStorageMaintenanceWorker(uniqueWorkName: String) {
        // Implemented by concrete implementation of `CreditCardsAddressesStorage`
    }
}

/**
 * An interface that defines methods for encrypting and decrypting a credit card number.
 */
interface CreditCardCrypto : KeyProvider {

    /**
     * Encrypt a [CreditCardNumber.Plaintext] using the provided key. A `null` result means a
     * bad key was provided. In that case caller should obtain a new key and try again.
     *
     * @param key The encryption key to encrypt the plaintext credit card number.
     * @param plaintextCardNumber A plaintext credit card number to be encrypted.
     * @return An encrypted credit card number or `null` if a bad [key] was provided.
     */
    fun encrypt(
        key: ManagedKey,
        plaintextCardNumber: CreditCardNumber.Plaintext,
    ): CreditCardNumber.Encrypted?

    /**
     * Decrypt a [CreditCardNumber.Encrypted] using the provided key. A `null` result means a
     * bad key was provided. In that case caller should obtain a new key and try again.
     *
     * @param key The encryption key to decrypt the decrypt credit card number.
     * @param encryptedCardNumber An encrypted credit card number to be decrypted.
     * @return A plaintext, non-encrypted credit card number or `null` if a bad [key] was provided.
     */
    fun decrypt(
        key: ManagedKey,
        encryptedCardNumber: CreditCardNumber.Encrypted,
    ): CreditCardNumber.Plaintext?
}

/**
 * A credit card number. This structure exists to provide better typing at the API surface.
 *
 * @property number Either a plaintext or a ciphertext of the credit card number, depending on the subtype.
 */
sealed class CreditCardNumber(val number: String) {
    /**
     * An encrypted credit card number.
     */
    @SuppressLint("ParcelCreator")
    @Parcelize
    data class Encrypted(private val data: String) : CreditCardNumber(data), Parcelable

    /**
     * A plaintext, non-encrypted credit card number.
     */
    data class Plaintext(private val data: String) : CreditCardNumber(data)
}

/**
 * Information about a credit card.
 *
 * @property guid The unique identifier for this credit card.
 * @property billingName The credit card billing name.
 * @property encryptedCardNumber The encrypted credit card number.
 * @property cardNumberLast4 The last 4 digits of the credit card number.
 * @property expiryMonth The credit card expiry month.
 * @property expiryYear The credit card expiry year.
 * @property cardType The credit card network ID.
 * @property timeCreated Time of creation in milliseconds from the unix epoch.
 * @property timeLastUsed Time of last use in milliseconds from the unix epoch.
 * @property timeLastModified Time of last modified in milliseconds from the unix epoch.
 * @property timesUsed Number of times the credit card was used.
 */
@SuppressLint("ParcelCreator")
@Parcelize
data class CreditCard(
    val guid: String,
    val billingName: String,
    val encryptedCardNumber: CreditCardNumber.Encrypted,
    val cardNumberLast4: String,
    val expiryMonth: Long,
    val expiryYear: Long,
    val cardType: String,
    val timeCreated: Long = 0L,
    val timeLastUsed: Long? = 0L,
    val timeLastModified: Long = 0L,
    val timesUsed: Long = 0L,
) : Parcelable {
    val obfuscatedCardNumber: String
        get() = ELLIPSES_START +
            ELLIPSIS + ELLIPSIS + ELLIPSIS + ELLIPSIS +
            cardNumberLast4 +
            ELLIPSES_END

    companion object {
        // Left-To-Right Embedding (LTE) mark
        const val ELLIPSES_START = "\u202A"

        // One dot ellipsis
        const val ELLIPSIS = "\u2022\u2060\u2006\u2060"

        // Pop Directional Formatting (PDF) mark
        const val ELLIPSES_END = "\u202C"
    }
}

/**
 * Credit card autofill entry.
 *
 * This contains the data needed to handle autofill but not the data related to the DB record.
 *
 * @property guid The unique identifier for this credit card.
 * @property name The credit card billing name.
 * @property number The credit card number.
 * @property expiryMonth The credit card expiry month.
 * @property expiryYear The credit card expiry year.
 * @property cardType The credit card network ID.
 */
@Parcelize
data class CreditCardEntry(
    val guid: String? = null,
    val name: String,
    val number: String,
    val expiryMonth: String,
    val expiryYear: String,
    val cardType: String,
) : Parcelable {
    val obfuscatedCardNumber: String
        get() = ELLIPSES_START +
            ELLIPSIS + ELLIPSIS + ELLIPSIS + ELLIPSIS +
            number.last4Digits() +
            ELLIPSES_END

    /**
     * Credit card expiry date formatted according to the locale. Returns an empty string if either
     * the expiration month or expiration year is not set.
     */
    val expiryDate: String
        get() {
            return if (expiryMonth.isEmpty() || expiryYear.isEmpty()) {
                ""
            } else {
                val dateFormat = SimpleDateFormat(DATE_PATTERN, Locale.getDefault())

                val calendar = Calendar.getInstance()
                calendar.set(Calendar.DAY_OF_MONTH, 1)
                // Subtract 1 from the expiry month since Calendar.Month is based on a 0-indexed.
                calendar.set(Calendar.MONTH, expiryMonth.toInt() - 1)
                calendar.set(Calendar.YEAR, expiryYear.toInt())

                dateFormat.format(calendar.time)
            }
        }

    /**
     * Whether this entry contains all data needed to be considered well-formed.
     */
    val isValid: Boolean
        get() = number.isNotEmpty() && expiryDate.isNotEmpty()

    companion object {
        // Date format pattern for the credit card expiry date.
        private const val DATE_PATTERN = "MM/yyyy"
    }
}

/**
 * Information about a new credit card.
 * Use this when creating a credit card via [CreditCardsAddressesStorage.addCreditCard].
 *
 * @property billingName The credit card billing name.
 * @property plaintextCardNumber A plaintext credit card number.
 * @property cardNumberLast4 The last 4 digits of the credit card number.
 * @property expiryMonth The credit card expiry month.
 * @property expiryYear The credit card expiry year.
 * @property cardType The credit card network ID.
 */
data class NewCreditCardFields(
    val billingName: String,
    val plaintextCardNumber: CreditCardNumber.Plaintext,
    val cardNumberLast4: String,
    val expiryMonth: Long,
    val expiryYear: Long,
    val cardType: String,
)

/**
 * Information about a new credit card.
 * Use this when creating a credit card via [CreditCardsAddressesStorage.updateAddress].
 *
 * @property billingName The credit card billing name.
 * @property cardNumber A [CreditCardNumber] that is either encrypted or plaintext. Passing in plaintext
 * version will update the stored credit card number.
 * @property cardNumberLast4 The last 4 digits of the credit card number.
 * @property expiryMonth The credit card expiry month.
 * @property expiryYear The credit card expiry year.
 * @property cardType The credit card network ID.
 */
data class UpdatableCreditCardFields(
    val billingName: String,
    val cardNumber: CreditCardNumber,
    val cardNumberLast4: String,
    val expiryMonth: Long,
    val expiryYear: Long,
    val cardType: String,
)

/**
 * Information about a address.
 *
 * @property guid The unique identifier for this address.
 * @property name A person's full name, typically made up of a first, middle and last name, e.g. John Joe Doe.
 * @property organization Organization.
 * @property streetAddress Street address.
 * @property addressLevel3 Sublocality (Suburb) name type.
 * @property addressLevel2 Locality (City/Town) name type.
 * @property addressLevel1 Province/State name type.
 * @property postalCode Postal code.
 * @property country Country.
 * @property tel Telephone number.
 * @property email E-mail address.
 * @property timeCreated Time of creation in milliseconds from the unix epoch.
 * @property timeLastUsed Time of last use in milliseconds from the unix epoch.
 * @property timeLastModified Time of last modified in milliseconds from the unix epoch.
 * @property timesUsed Number of times the address was used.
 */
@SuppressLint("ParcelCreator")
@Parcelize
data class Address(
    val guid: String,
    val name: String,
    val organization: String,
    val streetAddress: String,
    val addressLevel3: String,
    val addressLevel2: String,
    val addressLevel1: String,
    val postalCode: String,
    val country: String,
    val tel: String,
    val email: String,
    val timeCreated: Long = 0L,
    val timeLastUsed: Long? = 0L,
    val timeLastModified: Long = 0L,
    val timesUsed: Long = 0L,
) : Parcelable {

    /**
     * Returns a label for the [Address]. The ordering is based on the
     * priorities defined by the desktop code found here:
     * https://searchfox.org/mozilla-central/rev/d989c65584ded72c2de85cb40bede7ac2f176387/toolkit/components/formautofill/FormAutofillUtils.jsm#323
     */
    val addressLabel: String
        get() = listOf(
            name,
            streetAddress.toOneLineAddress(),
            addressLevel3,
            addressLevel2,
            organization,
            addressLevel1,
            country,
            postalCode,
            tel,
            email,
        ).filter { it.isNotEmpty() }.joinToString(", ")

    companion object {
        @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
        internal fun String.toOneLineAddress(): String =
            this.split("\n").joinToString(separator = " ") { it.trim() }
    }
}

/**
 * Information about a new address. This is what you pass to create or update an address.
 *
 * @property name A person's full name, typically made up of a first, middle and last name, e.g. John Joe Doe.
 * @property organization Organization.
 * @property streetAddress Street address.
 * @property addressLevel3 Sublocality (Suburb) name type.
 * @property addressLevel2 Locality (City/Town) name type.
 * @property addressLevel1 Province/State name type.
 * @property postalCode Postal code.
 * @property country Country.
 * @property tel Telephone number.
 * @property email E-mail address.
 */
data class UpdatableAddressFields(
    val name: String,
    val organization: String,
    val streetAddress: String,
    val addressLevel3: String,
    val addressLevel2: String,
    val addressLevel1: String,
    val postalCode: String,
    val country: String,
    val tel: String,
    val email: String,
)

/**
 * Provides a method for checking whether or not a given credit card can be stored.
 */
interface CreditCardValidationDelegate {

    /**
     * The result from validating a given [CreditCard] against the credit card storage. This will
     * include whether or not it can be created or updated.
     */
    sealed class Result {
        /**
         * Indicates that the [CreditCard] does not currently exist in the storage, and a new
         * credit card entry can be created.
         */
        object CanBeCreated : Result()

        /**
         * Indicates that a matching [CreditCard] was found in the storage, and the [CreditCard]
         * can be used to update its information.
         */
        data class CanBeUpdated(val foundCreditCard: CreditCard) : Result()
    }

    /**
     * Determines whether a [CreditCardEntry] can be added or updated in the credit card storage.
     *
     * @param creditCard [CreditCardEntry] to be added or updated in the credit card storage.
     * @return [Result] that indicates whether or not the [CreditCardEntry] should be saved or
     * updated.
     */
    suspend fun shouldCreateOrUpdate(creditCard: CreditCardEntry): Result
}

/**
 * Used to handle [Address] and [CreditCard] storage so that the underlying engine doesn't have to.
 * An instance of this should be attached to the Gecko runtime in order to be used.
 */
interface CreditCardsAddressesStorageDelegate : KeyProvider {

    /**
     * Decrypt a [CreditCardNumber.Encrypted] into its plaintext equivalent or `null` if
     * it fails to decrypt.
     *
     * @param key The encryption key to decrypt the decrypt credit card number.
     * @param encryptedCardNumber An encrypted credit card number to be decrypted.
     * @return A plaintext, non-encrypted credit card number.
     */
    suspend fun decrypt(
        key: ManagedKey,
        encryptedCardNumber: CreditCardNumber.Encrypted,
    ): CreditCardNumber.Plaintext?

    /**
     * Returns all stored addresses. This is called when the engine believes an address field
     * should be autofilled.
     *
     * @return A list of all stored addresses.
     */
    suspend fun onAddressesFetch(): List<Address>

    /**
     * Saves the given address to storage.
     *
     * @param address [Address] to be saved or updated in the address storage.
     */
    suspend fun onAddressSave(address: Address)

    /**
     * Returns all stored credit cards. This is called when the engine believes a credit card
     * field should be autofilled.
     *
     * @return A list of all stored credit cards.
     */
    suspend fun onCreditCardsFetch(): List<CreditCard>

    /**
     * Saves the given credit card to storage.
     *
     * @param creditCard [CreditCardEntry] to be saved or updated in the credit card storage.
     */
    suspend fun onCreditCardSave(creditCard: CreditCardEntry)
}
