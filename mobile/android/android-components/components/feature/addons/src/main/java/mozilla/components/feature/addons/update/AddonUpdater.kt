/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.addons.update

import android.content.Context
import androidx.annotation.VisibleForTesting
import androidx.core.app.NotificationManagerCompat
import androidx.work.Constraints
import androidx.work.CoroutineWorker
import androidx.work.Data
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.ExistingWorkPolicy
import androidx.work.NetworkType
import androidx.work.OneTimeWorkRequest
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.PeriodicWorkRequest
import androidx.work.PeriodicWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.WorkerParameters
import java.util.Date
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import kotlin.coroutines.resume
import kotlinx.coroutines.CancellableContinuation
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.action.WebExtensionAction
import mozilla.components.browser.state.state.extension.PendingExtensionUpdate
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.webextension.WebExtension
import mozilla.components.concept.engine.webextension.isUnsupported
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.addons.update.db.UpdateAttemptsDatabase
import mozilla.components.feature.addons.update.db.toEntity
import mozilla.components.feature.addons.worker.shouldReport
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.base.worker.Frequency
import mozilla.components.support.webextensions.WebExtensionSupport

/** Contract to define the behavior for updating addons. */
interface AddonUpdater {
    /**
     * Registers the given [addonId] for periodically check for new updates.
     *
     * @param addonId The unique id of the addon.
     */
    fun registerForFutureUpdates(addonId: String)

    /**
     * Unregisters the given [addonId] for periodically checking for new updates.
     *
     * @param addonId The unique id of the addon.
     */
    fun unregisterForFutureUpdates(addonId: String)

    /**
     * Try to perform an update on the given [addonId].
     *
     * @param addonId The unique id of the addon.
     */
    fun update(addonId: String)

    /**
     * Invoked when a web extension has changed its permissions while trying to update to a new version. This requires
     * user interaction as the updated extension will not be installed, until the user grants the new permissions.
     *
     * @param extension The new version of the [WebExtension] being updated.
     * @param newPermissions Contains a list of all the new required permissions.
     * @param newOrigins Contains a list of all the new required origins.
     * @param newDataCollectionPermissions Contains a list of all the new required data collection permissions.
     * @param onPermissionsGranted A callback to indicate if the new permissions are granted or not.
     */
    fun onUpdatePermissionRequest(
        extension: WebExtension,
        newPermissions: List<String>,
        newOrigins: List<String>,
        newDataCollectionPermissions: List<String>,
        onPermissionsGranted: (Boolean) -> Unit,
    )

    /**
     * Registers the [extensions] for periodic updates, if applicable. Built-in and unsupported extensions will not
     * update automatically.
     *
     * @param extensions The extensions to be registered for updates.
     */
    fun registerForFutureUpdates(extensions: List<WebExtension>) {
        extensions
            .filter { extension ->
                !extension.isBuiltIn() && !extension.isUnsupported()
            }
            .forEach { extension ->
                registerForFutureUpdates(extension.id)
            }
    }

    /** Indicates the status of a request for updating an addon. */
    sealed class Status {
        /** The addon is not part of the installed list. */
        data object NotInstalled : Status()

        /** The addon was successfully updated. */
        data object SuccessfullyUpdated : Status()

        /** The addon has not been updated since the last update. */
        data object NoUpdateAvailable : Status()

        /**
         * An error has happened while trying to update.
         *
         * @property message A string message describing what has happened.
         * @property exception The exception of the error.
         */
        data class Error(val message: String, val exception: Throwable) : Status()
    }

    /** Represents an attempt to update an add-on. */
    data class UpdateAttempt(val addonId: String, val date: Date, val status: Status?)
}

/**
 * An implementation of [AddonUpdater] that uses [WorkManager] for scheduling add-on updates.
 *
 * This class handles both periodic checks for updates and immediate update requests.
 *
 * An update that requires new permissions is paused until the user decides. The request is added in memory only to
 * [mozilla.components.browser.state.state.BrowserState.pendingExtensionUpdates], where the app answers it with
 * [PendingExtensionUpdate.complete] and removes it. If dismissed or ignored, it may reappear at the next update check.
 *
 * @property applicationContext The application context.
 * @param frequency How often periodic updates should be checked for. Defaults to once a day.
 * @property store The [BrowserStore] that holds the updates waiting for the user. It must be the store given to
 *   [mozilla.components.feature.addons.AddonManager] and [WebExtensionSupport].
 */
class DefaultAddonUpdater(
    private val applicationContext: Context,
    private val frequency: Frequency = Frequency(1, TimeUnit.DAYS),
    private val store: BrowserStore,
    ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : AddonUpdater {
    private val logger = Logger("DefaultAddonUpdater")

    private val scope = CoroutineScope(ioDispatcher + SupervisorJob())

    internal var updateAttemptStorage = UpdateAttemptStorage(applicationContext)

    init {
        // Clean up after the update notification of older versions. Deleting the channel also cancels any notification
        // still showing.
        scope.launch {
            NotificationManagerCompat.from(applicationContext).deleteNotificationChannel(LEGACY_NOTIFICATION_CHANNEL_ID)
            applicationContext.deleteSharedPreferences(LEGACY_STATUS_PREFERENCE_FILE)
        }
    }

    /** See [AddonUpdater.registerForFutureUpdates]. If an add-on is already registered nothing will happen. */
    override fun registerForFutureUpdates(addonId: String) {
        WorkManager.getInstance(applicationContext)
            .enqueueUniquePeriodicWork(
                getUniquePeriodicWorkName(addonId),
                ExistingPeriodicWorkPolicy.KEEP,
                createPeriodicWorkerRequest(addonId),
            )
        logger.info("registerForFutureUpdates $addonId")
    }

    /** See [AddonUpdater.unregisterForFutureUpdates] */
    override fun unregisterForFutureUpdates(addonId: String) {
        WorkManager.getInstance(applicationContext).cancelUniqueWork(getUniquePeriodicWorkName(addonId))
        logger.info("unregisterForFutureUpdates $addonId")
        scope.launch {
            updateAttemptStorage.remove(addonId)
        }
    }

    /** See [AddonUpdater.update] */
    override fun update(addonId: String) {
        WorkManager.getInstance(applicationContext)
            .beginUniqueWork(
                getUniqueImmediateWorkName(addonId),
                ExistingWorkPolicy.KEEP,
                createImmediateWorkerRequest(addonId),
            )
            .enqueue()
        logger.info("update $addonId")
    }

    /** See [AddonUpdater.onUpdatePermissionRequest] */
    override fun onUpdatePermissionRequest(
        extension: WebExtension,
        newPermissions: List<String>,
        newOrigins: List<String>,
        newDataCollectionPermissions: List<String>,
        onPermissionsGranted: (Boolean) -> Unit,
    ) {
        logger.info("onUpdatePermissionRequest ${extension.id}")

        // This has been added to keep backward compatibility for now. Previously, this was done in the caller.
        val allPermissions = newPermissions + newOrigins
        val shouldGrantWithoutPrompt =
            Addon.localizePermissions(allPermissions, applicationContext).isEmpty() &&
                Addon.localizeDataCollectionPermissions(newDataCollectionPermissions, applicationContext).isEmpty()
        if (shouldGrantWithoutPrompt) {
            logger.debug("onUpdatePermissionRequest ${extension.id} granted without prompting")
            onPermissionsGranted(true)
            return
        }

        val supersededUpdate = store.state.pendingExtensionUpdates[extension.id]
        store.dispatch(
            WebExtensionAction.UpdatePendingExtensionUpdateAction(
                PendingExtensionUpdate(
                    extensionId = extension.id,
                    permissions = newPermissions,
                    origins = newOrigins,
                    dataCollectionPermissions = newDataCollectionPermissions,
                    onPermissionsGranted = onPermissionsGranted,
                )
            )
        )
        supersededUpdate?.complete(granted = false)
    }

    @VisibleForTesting
    internal fun createImmediateWorkerRequest(addonId: String): OneTimeWorkRequest {
        val data = AddonUpdaterWorker.createWorkerData(addonId)
        val constraints = getWorkerConstraints()

        return OneTimeWorkRequestBuilder<AddonUpdaterWorker>()
            .setConstraints(constraints)
            .setInputData(data)
            .addTag(getUniqueImmediateWorkName(addonId))
            .addTag(WORK_TAG_IMMEDIATE)
            .build()
    }

    @VisibleForTesting
    internal fun createPeriodicWorkerRequest(addonId: String): PeriodicWorkRequest {
        val data = AddonUpdaterWorker.createWorkerData(addonId)
        val constraints = getWorkerConstraints()

        return PeriodicWorkRequestBuilder<AddonUpdaterWorker>(
                frequency.repeatInterval,
                frequency.repeatIntervalTimeUnit,
            )
            .setConstraints(constraints)
            .setInputData(data)
            .addTag(getUniquePeriodicWorkName(addonId))
            .addTag(WORK_TAG_PERIODIC)
            .build()
    }

    @VisibleForTesting
    internal fun getWorkerConstraints() =
        Constraints.Builder().setRequiresStorageNotLow(true).setRequiredNetworkType(NetworkType.CONNECTED).build()

    @VisibleForTesting
    internal fun getUniquePeriodicWorkName(addonId: String) = "$IDENTIFIER_PREFIX$addonId.periodicWork"

    @VisibleForTesting
    internal fun getUniqueImmediateWorkName(extensionId: String) = "$IDENTIFIER_PREFIX$extensionId.immediateWork"

    companion object {
        private const val IDENTIFIER_PREFIX = "mozilla.components.feature.addons.update."

        @VisibleForTesting
        internal const val LEGACY_NOTIFICATION_CHANNEL_ID = "mozilla.components.feature.addons.update.generic.channel"

        @VisibleForTesting
        internal const val LEGACY_STATUS_PREFERENCE_FILE =
            "mozilla.components.feature.addons.update.addons_updates_status_preference"

        /** Identifies all the workers that periodically check for new updates. */
        @VisibleForTesting
        internal const val WORK_TAG_PERIODIC = "mozilla.components.feature.addons.update.addonUpdater.periodicWork"

        /** Identifies all the workers that immediately check for new updates. */
        @VisibleForTesting
        internal const val WORK_TAG_IMMEDIATE = "mozilla.components.feature.addons.update.addonUpdater.immediateWork"
    }

    /** A storage implementation to persist [AddonUpdater.UpdateAttempt]s. */
    class UpdateAttemptStorage(context: Context) {
        @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
        internal var databaseInitializer = {
            UpdateAttemptsDatabase.get(context)
        }
        private val database by lazy { databaseInitializer() }

        /**
         * Persists the [AddonUpdater.UpdateAttempt] provided as a parameter.
         *
         * @param updateAttempt the [AddonUpdater.UpdateAttempt] to be stored.
         */
        internal suspend fun saveOrUpdate(updateAttempt: AddonUpdater.UpdateAttempt) {
            database.updateAttemptDao().insertOrUpdate(updateAttempt.toEntity())
        }

        /**
         * Finds the [AddonUpdater.UpdateAttempt] that match the [addonId] otherwise returns null.
         *
         * @param addonId the id to be used as filter in the search.
         */
        suspend fun findUpdateAttemptBy(addonId: String): AddonUpdater.UpdateAttempt? {
            return database.updateAttemptDao().getUpdateAttemptFor(addonId)?.toUpdateAttempt()
        }

        /**
         * Deletes the [AddonUpdater.UpdateAttempt] that match the [addonId] provided as a parameter.
         *
         * @param addonId the id of the [AddonUpdater.UpdateAttempt] to be deleted from the storage.
         */
        internal suspend fun remove(addonId: String) {
            database.updateAttemptDao().deleteUpdateAttempt(addonId)
        }
    }
}

/** A implementation which uses WorkManager APIs to perform addon updates. */
internal class AddonUpdaterWorker
@JvmOverloads
constructor(
    context: Context,
    private val params: WorkerParameters,
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : CoroutineWorker(context, params) {
    private val logger = Logger("AddonUpdaterWorker")
    internal var updateAttemptStorage = DefaultAddonUpdater.UpdateAttemptStorage(applicationContext)

    @VisibleForTesting internal var attemptScope = CoroutineScope(Dispatchers.IO)

    @Suppress("TooGenericExceptionCaught")
    override suspend fun doWork(): Result =
        withContext(mainDispatcher) {
            val extensionId = params.inputData.getString(KEY_DATA_EXTENSIONS_ID) ?: ""
            logger.info("Trying to update extension $extensionId")
            // We need to guarantee that we are not trying to update without
            // all the required state being initialized first.
            WebExtensionSupport.awaitInitialization()

            return@withContext suspendCancellableCoroutine { continuation ->
                val unfinished = AtomicReference<CancellableContinuation<Result>?>(continuation)
                var pendingUpdateJob: Job? = null

                fun finish(result: Result, status: AddonUpdater.Status) {
                    unfinished.getAndSet(null)?.let {
                        pendingUpdateJob?.cancel()
                        saveUpdateAttempt(extensionId, status)
                        it.resume(result)
                    }
                }

                continuation.invokeOnCancellation {
                    logger.info("Add-on update for $extensionId was cancelled.")
                    pendingUpdateJob?.cancel()
                    // cancel the in-progress addon update when the AddonManager exposes a cancellation API
                }

                try {
                    val manager = GlobalAddonDependencyProvider.requireAddonManager()

                    // An update requiring new permissions will be pending (potentially forever) until the user notices
                    // the request and answers it. Instead of waiting for that, report an error as soon as the request
                    // is pending.
                    pendingUpdateJob = attemptScope.launch {
                        // Note: this call only returns if the update waits for the user. Otherwise, the update check
                        // below finishes first (no update, or one applied right away), and finish() cancels this job.
                        manager.awaitPendingUpdateForExtension(extensionId)
                        val message = "Update of $extensionId not applied yet because its new permissions need approval"
                        logger.info(message)
                        finish(Result.failure(), AddonUpdater.Status.Error(message, Exception(message)))
                    }

                    manager.updateAddon(extensionId) { status ->
                        val result =
                            when (status) {
                                AddonUpdater.Status.NotInstalled -> {
                                    logger.error("Extension with id $extensionId removed from the update queue")
                                    Result.failure()
                                }
                                AddonUpdater.Status.NoUpdateAvailable -> {
                                    logger.info("There is no update available for $extensionId")
                                    Result.success()
                                }
                                AddonUpdater.Status.SuccessfullyUpdated -> {
                                    logger.info("Extension $extensionId successfully updated")
                                    Result.success()
                                }
                                is AddonUpdater.Status.Error -> {
                                    logger.error(
                                        "Error while trying to update extension $extensionId - status=${status.message}",
                                        status.exception,
                                    )
                                    Result.failure()
                                }
                            }
                        finish(result, status)
                    }
                } catch (exception: Exception) {
                    logger.error(
                        "Unable to update extension $extensionId - reason=${exception.message}",
                        exception,
                    )
                    if (exception.shouldReport()) {
                        GlobalAddonDependencyProvider.onCrash?.invoke(exception)
                    }
                    finish(Result.failure(), AddonUpdater.Status.Error(exception.message ?: "", exception))
                }
            }
        }

    @VisibleForTesting
    internal fun saveUpdateAttempt(extensionId: String, status: AddonUpdater.Status) {
        attemptScope.launch {
            updateAttemptStorage.saveOrUpdate(AddonUpdater.UpdateAttempt(extensionId, Date(), status))
        }
    }

    companion object {

        @VisibleForTesting
        internal const val KEY_DATA_EXTENSIONS_ID = "mozilla.components.feature.addons.update.KEY_DATA_EXTENSIONS_ID"

        @VisibleForTesting
        internal fun createWorkerData(extensionId: String) =
            Data.Builder().putString(KEY_DATA_EXTENSIONS_ID, extensionId).build()
    }
}
