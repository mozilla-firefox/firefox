/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.sync.autofill

import android.content.Context
import androidx.annotation.GuardedBy
import androidx.annotation.VisibleForTesting
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.WorkManager
import java.io.Closeable
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.async
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.appservices.autofill.AutofillApiException.NoSuchRecord
import mozilla.appservices.autofill.Store as RustAutofillStorage
import mozilla.appservices.autofill.createAutofillStoreWithStaticKeyManager
import mozilla.components.concept.storage.Address
import mozilla.components.concept.storage.CreditCard
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

const val AUTOFILL_DB_NAME = "autofill.sqlite"

/**
 * An implementation of [CreditCardsAddressesStorage] backed by the application-services' `autofill` library.
 *
 * @param context A [Context] used for disk access.
 * @param securePrefs A [SecureAbove22Preferences] wrapped in [Lazy] to avoid eager instantiation. Used for storing
 *   encryption key material.
 */
class AutofillCreditCardsAddressesStorage(
    private val context: Context,
    securePrefs: Lazy<SecureAbove22Preferences>,
) : CreditCardsAddressesStorage, SyncableStore, AutoCloseable {
    private val logger = Logger("AutofillCCAddressesStorage")

    private val coroutineContext by lazy { Dispatchers.IO }

    val crypto by lazy { AutofillCrypto(context, securePrefs.value, this) }

    private val scope by lazy { CoroutineScope(SupervisorJob() + coroutineContext) }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal val conn: Deferred<AutofillStorageConnection> by lazy {
        scope.async {
            val managedKey = crypto.getOrGenerateKey()
            AutofillStorageConnection.init(
                dbPath = context.getDatabasePath(AUTOFILL_DB_NAME).absolutePath,
                key = managedKey.key,
            )
            AutofillStorageConnection
        }
    }

    internal suspend fun getStorage(): RustAutofillStorage = conn.await().getStorage()

    /** "Warms up" this storage layer by establishing the database connection. */
    override suspend fun warmUp() =
        withContext(coroutineContext) {
            logElapsedTime(logger, "Warming up storage") { conn.await() }
            Unit
        }

    override suspend fun runMaintenance(dbSizeLimit: UInt) {
        getStorage().runMaintenance()
    }

    override suspend fun addCreditCard(creditCardFields: NewCreditCardFields): CreditCard =
        withContext(coroutineContext) {
            val updatableCreditCardFields =
                UpdatableCreditCardFields(
                    billingName = creditCardFields.billingName,
                    cardNumber = creditCardFields.cardNumber,
                    expiryMonth = creditCardFields.expiryMonth,
                    expiryYear = creditCardFields.expiryYear,
                    cardType = creditCardFields.cardType,
                    cardCvv = creditCardFields.cardCvv,
                )

            getStorage().addCreditCard(updatableCreditCardFields.into()).into()
        }

    override suspend fun updateCreditCard(
        guid: String,
        creditCardFields: UpdatableCreditCardFields,
    ) =
        withContext(coroutineContext) {
            getStorage().updateCreditCard(guid, creditCardFields.into())
        }

    override suspend fun getCreditCard(guid: String): CreditCard? =
        withContext(coroutineContext) {
            try {
                getStorage().getCreditCard(guid).into()
            } catch (e: NoSuchRecord) {
                null
            }
        }

    override suspend fun getAllCreditCards(): List<CreditCard> =
        withContext(coroutineContext) {
            getStorage().getAllCreditCards().map { it.into() }
        }

    override suspend fun countAllCreditCards(): Long =
        withContext(coroutineContext) {
            getStorage().countAllCreditCards()
        }

    override suspend fun deleteCreditCard(guid: String): Boolean =
        withContext(coroutineContext) {
            getStorage().deleteCreditCard(guid)
        }

    override suspend fun touchCreditCard(guid: String) =
        withContext(coroutineContext) {
            getStorage().touchCreditCard(guid)
        }

    override suspend fun addAddress(addressFields: UpdatableAddressFields): Address =
        withContext(coroutineContext) {
            getStorage().addAddress(addressFields.into()).into()
        }

    override suspend fun getAddress(guid: String): Address? =
        withContext(coroutineContext) {
            try {
                getStorage().getAddress(guid).into()
            } catch (e: NoSuchRecord) {
                null
            }
        }

    override suspend fun getAllAddresses(): List<Address> =
        withContext(coroutineContext) {
            getStorage().getAllAddresses().map { it.into() }
        }

    override suspend fun countAllAddresses(): Long =
        withContext(coroutineContext) {
            getStorage().countAllAddresses()
        }

    override suspend fun updateAddress(guid: String, address: UpdatableAddressFields) =
        withContext(coroutineContext) {
            getStorage().updateAddress(guid, address.into())
        }

    override suspend fun deleteAddress(guid: String): Boolean =
        withContext(coroutineContext) {
            getStorage().deleteAddress(guid)
        }

    override suspend fun touchAddress(guid: String) =
        withContext(coroutineContext) {
            getStorage().touchAddress(guid)
        }

    override fun getCreditCardCrypto(): AutofillCrypto {
        return crypto
    }

    override suspend fun scrubEncryptedData() =
        withContext(coroutineContext) {
            getStorage().scrubEncryptedData()
        }

    override fun registerWithSyncManager() {
        scope.launch {
            getStorage().registerWithSyncManager()
        }
    }

    override fun close() {
        scope.launch {
            conn.await().close()
            scope.cancel()
        }
    }

    /** Enqueues a periodic storage maintenance worker to WorkManager. */
    override fun registerStorageMaintenanceWorker() {
        WorkManager.getInstance(context)
            .enqueueUniquePeriodicWork(
                AutofillStorageWorker.UNIQUE_NAME,
                ExistingPeriodicWorkPolicy.KEEP,
                periodicStorageWorkRequest<AutofillStorageWorker>(tag = AutofillStorageWorker.UNIQUE_NAME) {
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

/** A singleton wrapping a [RustAutofillStorage] connection. */
internal object AutofillStorageConnection : Closeable {
    @GuardedBy("this") private var storage: RustAutofillStorage? = null

    internal fun init(dbPath: String = AUTOFILL_DB_NAME, key: String) =
        synchronized(this) {
            if (storage == null) {
                storage = createAutofillStoreWithStaticKeyManager(dbPath, key)
            }
        }

    internal fun getStorage(): RustAutofillStorage =
        synchronized(this) {
            check(storage != null) { "must call init first" }
            return storage!!
        }

    override fun close() =
        synchronized(this) {
            check(storage != null) { "must call init first" }
            storage!!.destroy()
            storage = null
        }
}
