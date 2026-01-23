/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.sync.autofill

import android.content.Context
import androidx.annotation.GuardedBy
import androidx.annotation.VisibleForTesting
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.WorkManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.withContext
import mozilla.appservices.autofill.AutofillApiException.NoSuchRecord
import mozilla.components.concept.storage.Address
import mozilla.components.concept.storage.CreditCard
import mozilla.components.concept.storage.CreditCardNumber
import mozilla.components.concept.storage.CreditCardsAddressesStorage
import mozilla.components.concept.storage.NewCreditCardFields
import mozilla.components.concept.storage.UpdatableAddressFields
import mozilla.components.concept.storage.UpdatableCreditCardFields
import mozilla.components.concept.storage.constraints
import mozilla.components.concept.storage.periodicStorageWorkRequest
import mozilla.components.concept.sync.SyncableStore
import mozilla.components.lib.dataprotect.SecureAbove22Preferences
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.utils.logElapsedTime
import java.io.Closeable
import mozilla.appservices.autofill.Store as RustAutofillStorage

const val AUTOFILL_DB_NAME = "autofill.sqlite"

/**
 * An implementation of [CreditCardsAddressesStorage] backed by the application-services' `autofill`
 * library.
 *
 * @param context A [Context] used for disk access.
 * @param securePrefs A [SecureAbove22Preferences] wrapped in [Lazy] to avoid eager instantiation.
 * Used for storing encryption key material.
 */
class AutofillCreditCardsAddressesStorage(
    private val context: Context,
    securePrefs: Lazy<SecureAbove22Preferences>,
) : CreditCardsAddressesStorage, SyncableStore, AutoCloseable {
    private val logger = Logger("AutofillCCAddressesStorage")

    private val coroutineContext by lazy { Dispatchers.IO }

    val crypto by lazy { AutofillCrypto(context, securePrefs.value, this) }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal val conn by lazy {
        AutofillStorageConnection.init(dbPath = context.getDatabasePath(AUTOFILL_DB_NAME).absolutePath)
        AutofillStorageConnection
    }

    /**
     * "Warms up" this storage layer by establishing the database connection.
     */
    override suspend fun warmUp() = withContext(coroutineContext) {
        logElapsedTime(logger, "Warming up storage") { conn }
        Unit
    }

    override suspend fun runMaintenance(dbSizeLimit: UInt) {
        conn.getStorage().runMaintenance()
    }

    override suspend fun addCreditCard(
        creditCardFields: NewCreditCardFields,
    ): CreditCard = withContext(coroutineContext) {
        val key = crypto.getOrGenerateKey()

        // Assume our key is good, and that this operation shouldn't fail.
        val encryptedCardNumber = crypto.encrypt(key, creditCardFields.plaintextCardNumber)!!
        val updatableCreditCardFields = UpdatableCreditCardFields(
            billingName = creditCardFields.billingName,
            cardNumber = encryptedCardNumber,
            cardNumberLast4 = creditCardFields.cardNumberLast4,
            expiryMonth = creditCardFields.expiryMonth,
            expiryYear = creditCardFields.expiryYear,
            cardType = creditCardFields.cardType,
        )

        conn.getStorage().addCreditCard(updatableCreditCardFields.into()).into()
    }

    override suspend fun updateCreditCard(
        guid: String,
        creditCardFields: UpdatableCreditCardFields,
    ) = withContext(coroutineContext) {
        val updatableCreditCardFields = when (creditCardFields.cardNumber) {
            // If credit card number changed, we need to encrypt it.
            is CreditCardNumber.Plaintext -> {
                val key = crypto.getOrGenerateKey()
                // Assume our key is good, and that this operation shouldn't fail.
                val encryptedCardNumber = crypto.encrypt(
                    key,
                    creditCardFields.cardNumber as CreditCardNumber.Plaintext,
                )!!
                UpdatableCreditCardFields(
                    billingName = creditCardFields.billingName,
                    cardNumber = encryptedCardNumber,
                    cardNumberLast4 = creditCardFields.cardNumberLast4,
                    expiryMonth = creditCardFields.expiryMonth,
                    expiryYear = creditCardFields.expiryYear,
                    cardType = creditCardFields.cardType,
                )
            }
            // If card number didn't change, we're just round-tripping an existing encrypted version.
            is CreditCardNumber.Encrypted -> {
                UpdatableCreditCardFields(
                    billingName = creditCardFields.billingName,
                    cardNumber = creditCardFields.cardNumber,
                    cardNumberLast4 = creditCardFields.cardNumberLast4,
                    expiryMonth = creditCardFields.expiryMonth,
                    expiryYear = creditCardFields.expiryYear,
                    cardType = creditCardFields.cardType,
                )
            }
        }
        conn.getStorage().updateCreditCard(guid, updatableCreditCardFields.into())
    }

    override suspend fun getCreditCard(guid: String): CreditCard? = withContext(coroutineContext) {
        try {
            conn.getStorage().getCreditCard(guid).into()
        } catch (e: NoSuchRecord) {
            null
        }
    }

    override suspend fun getAllCreditCards(): List<CreditCard> = withContext(coroutineContext) {
        conn.getStorage().getAllCreditCards().map { it.into() }
    }

    override suspend fun deleteCreditCard(guid: String): Boolean = withContext(coroutineContext) {
        conn.getStorage().deleteCreditCard(guid)
    }

    override suspend fun touchCreditCard(guid: String) = withContext(coroutineContext) {
        conn.getStorage().touchCreditCard(guid)
    }

    override suspend fun addAddress(addressFields: UpdatableAddressFields): Address =
        withContext(coroutineContext) {
            conn.getStorage().addAddress(addressFields.into()).into()
        }

    override suspend fun getAddress(guid: String): Address? = withContext(coroutineContext) {
        try {
            conn.getStorage().getAddress(guid).into()
        } catch (e: NoSuchRecord) {
            null
        }
    }

    override suspend fun getAllAddresses(): List<Address> = withContext(coroutineContext) {
        conn.getStorage().getAllAddresses().map { it.into() }
    }

    override suspend fun updateAddress(guid: String, address: UpdatableAddressFields) =
        withContext(coroutineContext) {
            conn.getStorage().updateAddress(guid, address.into())
        }

    override suspend fun deleteAddress(guid: String): Boolean = withContext(coroutineContext) {
        conn.getStorage().deleteAddress(guid)
    }

    override suspend fun touchAddress(guid: String) = withContext(coroutineContext) {
        conn.getStorage().touchAddress(guid)
    }

    override fun getCreditCardCrypto(): AutofillCrypto {
        return crypto
    }

    override suspend fun scrubEncryptedData() = withContext(coroutineContext) {
        conn.getStorage().scrubEncryptedData()
    }

    override fun registerWithSyncManager() {
        conn.getStorage().registerWithSyncManager()
    }

    override fun close() {
        coroutineContext.cancel()
        conn.close()
    }

    /**
     * Enqueues a periodic storage maintenance worker to WorkManager.
     */
    override fun registerStorageMaintenanceWorker() {
        WorkManager.getInstance(context).enqueueUniquePeriodicWork(
            AutofillStorageWorker.UNIQUE_NAME,
            ExistingPeriodicWorkPolicy.KEEP,
            periodicStorageWorkRequest<AutofillStorageWorker>(
                tag = AutofillStorageWorker.UNIQUE_NAME,
            ) {
                constraints {
                    setRequiresBatteryNotLow(true)
                    setRequiresDeviceIdle(true)
                }
            },
        )
    }

    override fun unregisterStorageMaintenanceWorker(uniqueWorkName: String) {
        WorkManager.getInstance(context).also {
            it.cancelUniqueWork(AutofillStorageWorker.UNIQUE_NAME)
            it.cancelAllWorkByTag(AutofillStorageWorker.UNIQUE_NAME)
        }
    }
}

/**
 * A singleton wrapping a [RustAutofillStorage] connection.
 */
internal object AutofillStorageConnection : Closeable {
    @GuardedBy("this")
    private var storage: RustAutofillStorage? = null

    internal fun init(dbPath: String = AUTOFILL_DB_NAME) = synchronized(this) {
        if (storage == null) {
            storage = RustAutofillStorage(dbPath)
        }
    }

    internal fun getStorage(): RustAutofillStorage = synchronized(this) {
        check(storage != null) { "must call init first" }
        return storage!!
    }

    override fun close() = synchronized(this) {
        check(storage != null) { "must call init first" }
        storage!!.destroy()
        storage = null
    }
}
