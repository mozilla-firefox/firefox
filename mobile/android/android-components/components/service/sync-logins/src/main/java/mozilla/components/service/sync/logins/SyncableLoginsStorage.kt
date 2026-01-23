/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.sync.logins

import android.content.Context
import android.content.SharedPreferences
import androidx.core.content.edit
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.WorkManager
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.appservices.logins.DatabaseLoginsStorage
import mozilla.appservices.logins.KeyRegenerationEventReason
import mozilla.appservices.logins.recordKeyRegenerationEvent
import mozilla.components.concept.storage.KeyGenerationReason
import mozilla.components.concept.storage.Login
import mozilla.components.concept.storage.LoginEntry
import mozilla.components.concept.storage.LoginsStorage
import mozilla.components.concept.storage.constraints
import mozilla.components.concept.storage.periodicStorageWorkRequest
import mozilla.components.concept.sync.SyncableStore
import mozilla.components.lib.dataprotect.SecureAbove22Preferences
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.utils.logElapsedTime

// Current database
const val DB_NAME = "logins2.sqlite"

// Name of our preferences file
const val PREFS_NAME = "logins"

// Name of key that checks if we've cleaned undecryptable keys
const val UNDECRYPTABLE_LOGINS_CLEANED_KEY = "logins_undecryptable_cleaned"

/**
 * The telemetry ping from a successful sync
 */
typealias SyncTelemetryPing = mozilla.appservices.sync15.SyncTelemetryPing

/**
 * The base class of all errors emitted by logins storage.
 *
 * Concrete instances of this class are thrown for operations which are
 * not expected to be handled in a meaningful way by the application.
 *
 * For example, caught Rust panics, SQL errors, failure to generate secure
 * random numbers, etc. are all examples of things which will result in a
 * concrete `LoginsApiException`.
 */
typealias LoginsApiException = mozilla.appservices.logins.LoginsApiException

/**
 * This indicates that the authentication information (e.g. the [SyncUnlockInfo])
 * provided to [AsyncLoginsStorage.sync] is invalid. This often indicates that it's
 * stale and should be refreshed with FxA (however, care should be taken not to
 * get into a loop refreshing this information).
 */
typealias SyncAuthInvalidException = mozilla.appservices.logins.LoginsApiException.SyncAuthInvalid

/**
 * This is thrown if `update()` is performed with a record whose GUID
 * does not exist.
 */
typealias NoSuchRecordException = mozilla.appservices.logins.LoginsApiException.NoSuchRecord

/**
 * This is thrown on attempts to insert or update a record so that it
 * is no longer valid, where "invalid" is defined as such:
 *
 * - A record with a blank `password` is invalid.
 * - A record with a blank `hostname` is invalid.
 * - A record that doesn't have a `formSubmitURL` nor a `httpRealm` is invalid.
 * - A record that has both a `formSubmitURL` and a `httpRealm` is invalid.
 */
typealias InvalidRecordException = mozilla.appservices.logins.LoginsApiException.InvalidRecord

/**
 * Error encrypting/decrypting logins data
 */
typealias InvalidKey = mozilla.appservices.logins.LoginsApiException.InvalidKey

/**
 * Implements [LoginsStorage] and [SyncableStore] using the application-services logins library.
 *
 * Synchronization is handled via the SyncManager by calling [registerWithSyncManager]
 */
class SyncableLoginsStorage(
    private val context: Context,
    private val securePrefs: Lazy<SecureAbove22Preferences>,
    private val coroutineDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : LoginsStorage, SyncableStore, AutoCloseable {
    private val logger = Logger("SyncableLoginsStorage")
    private val coroutineContext by lazy { coroutineDispatcher }
    private val prefs: SharedPreferences by lazy {
        context.getSharedPreferences(
            "sync.logins.prefs",
            Context.MODE_PRIVATE,
        )
    }
    val crypto by lazy { LoginsCrypto(context, securePrefs.value, this) }

    private val conn: Deferred<DatabaseLoginsStorage> = CoroutineScope(coroutineContext).async {
        val managedKey = crypto.getOrGenerateKey()
        val key = managedKey.key
        val keyManager = object : mozilla.appservices.logins.KeyManager {
            override fun getKey(): ByteArray {
                return key.toByteArray()
            }
        }
        val path = context.getDatabasePath(DB_NAME)
        val pathExisted = path.exists()
        val storage = DatabaseLoginsStorage(path.absolutePath, keyManager)
        // If the path existed, but we generated a new key, then the key can't decrypt any existing
        // logins.  Run wipeLocal, to try to recover
        // (https://bugzilla.mozilla.org/show_bug.cgi?id=1970409)
        if (managedKey.wasGenerated == KeyGenerationReason.New && pathExisted) {
            recordKeyRegenerationEvent(KeyRegenerationEventReason.Other)
            tryWithStorageOr(Unit) { wipeLocal() }
        }
        storage
    }

    internal suspend fun getStorage(): DatabaseLoginsStorage = conn.await()

    /**
     * "Warms up" this storage layer by establishing the database connection.
     */
    override suspend fun warmUp() = withContext(coroutineContext) {
        logElapsedTime(logger, "Warming up storage") { conn.await() }
        Unit
    }

    override suspend fun runMaintenance(dbSizeLimit: UInt) {
         getStorage().runMaintenance()
    }

    /**
     * @throws [LoginsApiException] if the storage is locked, and on unexpected
     *              errors (IO failure, rust panics, etc)
     */
    @Throws(LoginsApiException::class)
    override suspend fun wipeLocal() = withContext(coroutineContext) {
         tryWithStorageOr(Unit) { wipeLocal() }
    }

    /**
     * @throws [LoginsApiException] if the storage is locked, and on unexpected
     *              errors (IO failure, rust panics, etc)
     */
    @Throws(LoginsApiException::class)
    override suspend fun delete(guid: String): Boolean = withContext(coroutineContext) {
        tryWithStorageOr(false) { delete(guid) }
    }

    /**
     * @throws [LoginsApiException] if the storage is locked, and on unexpected
     *              errors (IO failure, rust panics, etc)
     */
    @Throws(LoginsApiException::class)
    override suspend fun get(guid: String): Login? = withContext(coroutineContext) {
        tryWithStorageOr(null) { get(guid)?.toLogin() }
    }

    /**
     * @throws [NoSuchRecordException] if the login does not exist.
     * @throws [LoginsApiException] if the storage is locked, and on unexpected
     *              errors (IO failure, rust panics, etc)
     */
    @Throws(NoSuchRecordException::class, LoginsApiException::class)
    override suspend fun touch(guid: String) = withContext(coroutineContext) {
        tryWithStorageOr(Unit) { touch(guid) }
    }

    /**
     * @throws [LoginsApiException] if the storage is locked, and on unexpected
     *              errors (IO failure, rust panics, etc)
     */
    @Throws(LoginsApiException::class)
    override suspend fun list(): List<Login> = withContext(coroutineContext) {
        tryWithStorageOr(listOf()) { list().map { it.toLogin() } }
    }

    /**
     * @throws [InvalidRecordException] if the record is invalid.
     * @throws [InvalidKey] if the encryption key can't decrypt the login
     * @throws [LoginsApiException] if the storage is locked, and on unexpected
     *              errors (IO failure, rust panics, etc)
     */
    @Throws(InvalidKey::class, InvalidRecordException::class, LoginsApiException::class)
    override suspend fun add(entry: LoginEntry) = withContext(coroutineContext) {
        getStorage().add(entry.toLoginEntry()).toLogin()
    }

    /**
     * @throws [NoSuchRecordException] if the login does not exist.
     * @throws [InvalidKey] if the encryption key can't decrypt the login
     * @throws [InvalidRecordException] if the update would create an invalid record.
     * @throws [LoginsApiException] if the storage is locked, and on unexpected
     *              errors (IO failure, rust panics, etc)
     */
    @Throws(
        InvalidKey::class,
        NoSuchRecordException::class,
        InvalidRecordException::class,
        LoginsApiException::class,
    )
    override suspend fun update(guid: String, entry: LoginEntry) = withContext(coroutineContext) {
        getStorage().update(guid, entry.toLoginEntry()).toLogin()
    }

    /**
     * @throws [InvalidRecordException] if the update would create an invalid record.
     * @throws [InvalidKey] if the encryption key can't decrypt the login
     * @throws [LoginsApiException] if the storage is locked, and on unexpected
     *              errors (IO failure, rust panics, etc)
     */
    @Throws(InvalidKey::class, InvalidRecordException::class, LoginsApiException::class)
    override suspend fun addOrUpdate(entry: LoginEntry) = withContext(coroutineContext) {
        getStorage().addOrUpdate(entry.toLoginEntry()).toLogin()
    }

    override fun registerWithSyncManager() {
        CoroutineScope(coroutineContext).launch {
            // before registering with the syncmanager we should delete undecryptable logins
            // sync will do the right thing and get them from the server if any were still valid
            runUndecryptableCleanupIfNeeded()
            tryWithStorageOr(Unit) { registerWithSyncManager() }
        }
    }

    /**
     * @throws [LoginsApiException] On unexpected errors (IO failure, rust panics, etc)
     */
    @Throws(LoginsApiException::class)
    override suspend fun getByBaseDomain(origin: String): List<Login> = withContext(coroutineContext) {
        tryWithStorageOr(listOf()) { getByBaseDomain(origin).map { it.toLogin() } }
    }

    /**
     * @throws [InvalidKey] if the encryption key can't decrypt the login
     * @throws [LoginsApiException] On unexpected errors (IO failure, rust panics, etc)
     */
    @Throws(LoginsApiException::class)
    override suspend fun findLoginToUpdate(entry: LoginEntry): Login? = withContext(coroutineContext) {
        tryWithStorageOr(null) { findLoginToUpdate(entry.toLoginEntry())?.toLogin() }
    }

    override fun close() {
        CoroutineScope(coroutineContext).launch {
            tryWithStorageOr(Unit) { close() }
            this.cancel()
        }
    }

    private suspend fun <T> tryWithStorageOr(default: T, operation: DatabaseLoginsStorage.() -> T): T {
        return try {
            getStorage().operation()
        } catch (e: LoginsApiException) {
            logger.error("Error during logins operation", e)
            default
        }
    }

    /**
     * If we've lost the encryption key or other issues that prevent us from decrypting
     * existing logins, we run a cleanup to purge those records. We only need to do
     * this once for existing undecryptable records and if ever user needs
     * new keys, the new generation flow will automatically do this for us
     * @throws [LoginsApiException] On unexpected errors (IO failure, rust panics, etc)
     */
    @Throws(LoginsApiException::class)
    suspend fun runUndecryptableCleanupIfNeeded() = withContext(coroutineContext) {
        // We use an int preference here to track if we've already ran the cleanup,
        // and to allow us to run it again by bumping the value of the check
        var cleanedPref = prefs.getInt(UNDECRYPTABLE_LOGINS_CLEANED_KEY, 0)
        if (cleanedPref < 1) {
            tryWithStorageOr(Unit) {
                deleteUndecryptableLoginsAndRecordMetrics()
            }
            prefs.edit { putInt(UNDECRYPTABLE_LOGINS_CLEANED_KEY, ++cleanedPref) }
        }
    }

    /**
     * Enqueues a periodic storage maintenance worker to WorkManager.
     */
    override fun registerStorageMaintenanceWorker() {
        WorkManager.getInstance(context).enqueueUniquePeriodicWork(
            SyncableLoginsStorageWorker.UNIQUE_NAME,
            ExistingPeriodicWorkPolicy.KEEP,
            periodicStorageWorkRequest<SyncableLoginsStorageWorker>(
                tag = SyncableLoginsStorageWorker.UNIQUE_NAME,
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
            it.cancelUniqueWork(SyncableLoginsStorageWorker.UNIQUE_NAME)
            it.cancelAllWorkByTag(SyncableLoginsStorageWorker.UNIQUE_NAME)
        }
    }
}
