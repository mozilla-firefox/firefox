/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.addons.update

import android.app.Notification
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.os.IBinder
import android.text.SpannableStringBuilder
import android.text.Spanned
import androidx.annotation.VisibleForTesting
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.edit
import androidx.core.text.HtmlCompat
import androidx.core.text.toSpanned
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
import mozilla.components.feature.addons.R
import mozilla.components.feature.addons.update.db.UpdateAttemptsDatabase
import mozilla.components.feature.addons.update.db.toEntity
import mozilla.components.feature.addons.worker.shouldReport
import mozilla.components.support.base.ids.SharedIdsHelper
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.base.worker.Frequency
import mozilla.components.support.ktx.android.notification.ChannelData
import mozilla.components.support.ktx.android.notification.ensureNotificationChannelExists
import mozilla.components.support.webextensions.WebExtensionSupport
import mozilla.components.ui.icons.R as iconsR

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
@Suppress("LargeClass")
class DefaultAddonUpdater(
    private val applicationContext: Context,
    private val frequency: Frequency = Frequency(1, TimeUnit.DAYS),
    private val store: BrowserStore,
    ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : AddonUpdater {
    private val logger = Logger("DefaultAddonUpdater")

    private val scope = CoroutineScope(ioDispatcher + SupervisorJob())

    internal var updateAttemptStorage = UpdateAttemptStorage(applicationContext)

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

    @VisibleForTesting
    internal fun createNotification(
        extension: WebExtension,
        newPermissions: List<String>,
        newDataCollectionPermissions: List<String>,
        notificationId: Int,
    ): Notification {
        val channel =
            ChannelData(
                NOTIFICATION_CHANNEL_ID,
                R.string.mozac_feature_addons_updater_notification_channel_2,
                NotificationManagerCompat.IMPORTANCE_LOW,
            )
        val channelId = ensureNotificationChannelExists(applicationContext, channel)

        logger.info("Created update notification for add-on ${extension.id}")
        return NotificationCompat.Builder(applicationContext, channelId)
            .setSmallIcon(iconsR.drawable.mozac_ic_extension_24)
            .setContentTitle(getNotificationTitle(extension))
            .setPriority(NotificationCompat.PRIORITY_MAX)
            .setStyle(
                NotificationCompat.BigTextStyle()
                    .bigText(createContentText(newPermissions, newDataCollectionPermissions))
            )
            .addAction(createAllowAction(extension, notificationId))
            .addAction(createDenyAction(extension, notificationId))
            .setAutoCancel(true)
            .build()
    }

    private fun getNotificationTitle(extension: WebExtension): String {
        return applicationContext.getString(
            R.string.mozac_feature_addons_updater_notification_title_2,
            extension.getMetadata()?.name,
        )
    }

    @VisibleForTesting
    internal fun createContentText(permissions: List<String>, dataCollectionPermissions: List<String>): Spanned {
        val hasPermissions = permissions.isNotEmpty()
        val hasDataCollectionPermissions = dataCollectionPermissions.isNotEmpty()

        val intro = applicationContext.getString(R.string.mozac_feature_addons_updater_notification_short_intro)
        val builder = SpannableStringBuilder(HtmlCompat.fromHtml("$intro<br>", HtmlCompat.FROM_HTML_MODE_COMPACT))

        if (hasPermissions) {
            val localizedPermissions = Addon.localizePermissions(permissions, applicationContext, forUpdate = true)
            var permissionsText =
                applicationContext.getString(
                    R.string.mozac_feature_addons_updater_notification_heading_permissions,
                    localizedPermissions.joinToString(" "),
                )
            // When we have data collection permissions, we need to truncate the content of this section
            // so that the data collection permissions section can also be displayed in the notification.
            permissionsText = maybeAbbreviate(permissionsText, hasDataCollectionPermissions)

            builder.append(HtmlCompat.fromHtml(permissionsText, HtmlCompat.FROM_HTML_MODE_COMPACT))

            if (hasDataCollectionPermissions) {
                builder.append(HtmlCompat.fromHtml("<br>", HtmlCompat.FROM_HTML_MODE_COMPACT))
            }
        }

        if (hasDataCollectionPermissions) {
            val localizedPermissions =
                Addon.localizeDataCollectionPermissions(
                    dataCollectionPermissions,
                    applicationContext,
                )
            var permissionsText =
                applicationContext.getString(
                    R.string.mozac_feature_addons_updater_notification_heading_data_collection_permissions,
                    Addon.formatLocalizedDataCollectionPermissions(localizedPermissions),
                )
            // When we have permissions, we need to truncate the content of this section so that the
            // permissions section can also be displayed above (along with this one).
            permissionsText = maybeAbbreviate(permissionsText, hasPermissions)

            builder.append(HtmlCompat.fromHtml(permissionsText, HtmlCompat.FROM_HTML_MODE_COMPACT))
        }

        return builder.toSpanned()
    }

    private fun maybeAbbreviate(text: String, saveSpaceForOtherSection: Boolean): String {
        // We take up to NARROW_MAX_LENGTH characters when we need to save some space for the other section in the
        // notification. Otherwise, we still truncate the text because Android system notifications cannot be too tall,
        // and we need to account for the short intro text.
        val maxLength =
            if (saveSpaceForOtherSection) {
                NARROW_MAX_LENGTH
            } else {
                WIDE_MAX_LENGTH
            }

        var result = text
        if (result.length > maxLength) {
            result = result.take(maxLength - 1).plus(Typography.ellipsis)
        }
        return result
    }

    @VisibleForTesting
    internal fun createNotificationIntent(extId: String, actionString: String): Intent {
        return Intent(applicationContext, NotificationHandlerService::class.java).apply {
            action = actionString
            putExtra(NOTIFICATION_EXTRA_ADDON_ID, extId)
        }
    }

    @VisibleForTesting
    internal fun createAllowAction(ext: WebExtension, requestCode: Int): NotificationCompat.Action {
        val allowIntent = createNotificationIntent(ext.id, NOTIFICATION_ACTION_ALLOW)
        val allowPendingIntent =
            PendingIntent.getService(
                applicationContext,
                requestCode,
                allowIntent,
                PendingIntent.FLAG_IMMUTABLE,
            )

        val allowText = applicationContext.getString(R.string.mozac_feature_addons_updater_notification_update_button)

        return NotificationCompat.Action.Builder(
                iconsR.drawable.mozac_ic_extension_24,
                allowText,
                allowPendingIntent,
            )
            .build()
    }

    @VisibleForTesting
    internal fun createDenyAction(ext: WebExtension, requestCode: Int): NotificationCompat.Action {
        val denyIntent = createNotificationIntent(ext.id, NOTIFICATION_ACTION_DENY)
        val denyPendingIntent =
            PendingIntent.getService(
                applicationContext,
                requestCode,
                denyIntent,
                PendingIntent.FLAG_IMMUTABLE,
            )

        val denyText = applicationContext.getString(R.string.mozac_feature_addons_updater_notification_cancel_button)

        return NotificationCompat.Action.Builder(
                iconsR.drawable.mozac_ic_extension_24,
                denyText,
                denyPendingIntent,
            )
            .build()
    }

    companion object {
        private const val NOTIFICATION_CHANNEL_ID = "mozilla.components.feature.addons.update.generic.channel"

        @VisibleForTesting
        internal const val NOTIFICATION_EXTRA_ADDON_ID = "mozilla.components.feature.addons.update.extra.extensionId"

        @VisibleForTesting internal const val NOTIFICATION_TAG = "mozilla.components.feature.addons.update.addonUpdater"

        @VisibleForTesting
        internal const val NOTIFICATION_ACTION_DENY =
            "mozilla.components.feature.addons.update.NOTIFICATION_ACTION_DENY"

        @VisibleForTesting
        internal const val NOTIFICATION_ACTION_ALLOW =
            "mozilla.components.feature.addons.update.NOTIFICATION_ACTION_ALLOW"
        private const val IDENTIFIER_PREFIX = "mozilla.components.feature.addons.update."

        /** Identifies all the workers that periodically check for new updates. */
        @VisibleForTesting
        internal const val WORK_TAG_PERIODIC = "mozilla.components.feature.addons.update.addonUpdater.periodicWork"

        /** Identifies all the workers that immediately check for new updates. */
        @VisibleForTesting
        internal const val WORK_TAG_IMMEDIATE = "mozilla.components.feature.addons.update.addonUpdater.immediateWork"

        @VisibleForTesting internal const val NARROW_MAX_LENGTH = 160

        @VisibleForTesting internal const val WIDE_MAX_LENGTH = 320
    }

    /** Handles notification actions related to addons that require additional permissions to be updated. */
    class NotificationHandlerService : Service() {

        private val logger = Logger("NotificationHandlerService")

        @VisibleForTesting internal var context: Context = this

        internal fun onHandleIntent(intent: Intent?) {
            val addonId = intent?.getStringExtra(NOTIFICATION_EXTRA_ADDON_ID) ?: return

            when (intent.action) {
                NOTIFICATION_ACTION_ALLOW -> {
                    handleAllowAction(addonId)
                }
                NOTIFICATION_ACTION_DENY -> {
                    removeNotification(addonId)
                }
            }
        }

        @VisibleForTesting
        internal fun removeNotification(addonId: String) {
            val notificationId = getNotificationId(context, addonId)
            NotificationManagerCompat.from(context).cancel(notificationId)
        }

        @VisibleForTesting
        internal fun handleAllowAction(addonId: String) {
            val storage = UpdateStatusStorage()
            logger.info("Addon $addonId permissions were granted")
            storage.markAsAllowed(context, addonId)
            GlobalAddonDependencyProvider.requireAddonUpdater().update(addonId)
            removeNotification(addonId)
        }

        override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
            onHandleIntent(intent)
            return super.onStartCommand(intent, flags, startId)
        }

        override fun onBind(intent: Intent?): IBinder? = null

        companion object {
            @VisibleForTesting
            internal fun getNotificationId(context: Context, addonId: String): Int {
                return SharedIdsHelper.getIdForTag(context, "$NOTIFICATION_TAG.$addonId")
            }
        }
    }

    /** Stores the status of an addon update. */
    internal class UpdateStatusStorage {

        fun isPreviouslyAllowed(context: Context, addonId: String) = getData(context).contains(addonId)

        @Synchronized
        fun markAsAllowed(context: Context, addonId: String) {
            val allowSet = getData(context)
            allowSet.add(addonId)
            setData(context, allowSet)
        }

        @Synchronized
        fun markAsUnallowed(context: Context, addonId: String) {
            val allowSet = getData(context)
            allowSet.remove(addonId)
            setData(context, allowSet)
        }

        fun clear(context: Context) {
            getSettings(context).edit { clear() }
        }

        @VisibleForTesting
        internal fun getData(context: Context): MutableSet<String> {
            val data = requireNotNull(getSettings(context).getStringSet(KEY_ALLOWED_SET, mutableSetOf()))
            // We must return a mutable copy of the allowed set because we want to persist it on disk, and changes to
            // this allowed set can only be detected when a new set is passed to `setData()`.
            return data.toMutableSet()
        }

        private fun getSettings(context: Context) = getSharedPreferences(context)

        private fun setData(context: Context, allowSet: MutableSet<String>) {
            // Get rid of the old allowed set, if it still exists.
            if (getSettings(context).contains(OLD_KEY_ALLOWED_SET)) {
                clear(context)
            }

            getSettings(context).edit { putStringSet(KEY_ALLOWED_SET, allowSet) }
        }

        private fun getSharedPreferences(context: Context): SharedPreferences {
            return context.getSharedPreferences(PREFERENCE_FILE, Context.MODE_PRIVATE)
        }

        companion object {
            internal const val PREFERENCE_FILE =
                "mozilla.components.feature.addons.update.addons_updates_status_preference"
            internal const val OLD_KEY_ALLOWED_SET = "mozilla.components.feature.addons.update.KEY_ALLOWED_SET"
            internal const val KEY_ALLOWED_SET = "mozilla.components.feature.addons.update.KEY_ALLOWED_SET_2"
        }
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
