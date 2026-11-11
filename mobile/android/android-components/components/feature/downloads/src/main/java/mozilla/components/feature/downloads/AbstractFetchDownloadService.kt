/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.downloads

import android.app.DownloadManager.ACTION_DOWNLOAD_COMPLETE
import android.app.DownloadManager.EXTRA_DOWNLOAD_ID
import android.app.ForegroundServiceStartNotAllowedException
import android.app.Notification
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.Build.VERSION.SDK_INT
import android.os.IBinder
import android.webkit.MimeTypeMap
import android.widget.Toast
import androidx.annotation.ColorRes
import androidx.annotation.GuardedBy
import androidx.annotation.VisibleForTesting
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import androidx.core.net.toUri
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import kotlin.random.Random
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import mozilla.components.browser.state.action.DownloadAction
import mozilla.components.browser.state.state.content.DownloadState
import mozilla.components.browser.state.state.content.DownloadState.Status
import mozilla.components.browser.state.state.content.DownloadState.Status.CANCELLED
import mozilla.components.browser.state.state.content.DownloadState.Status.COMPLETED
import mozilla.components.browser.state.state.content.DownloadState.Status.DOWNLOADING
import mozilla.components.browser.state.state.content.DownloadState.Status.FAILED
import mozilla.components.browser.state.state.content.DownloadState.Status.INITIATED
import mozilla.components.browser.state.state.content.DownloadState.Status.PAUSED
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.fetch.Client
import mozilla.components.concept.fetch.Headers.Names.CONTENT_RANGE
import mozilla.components.concept.fetch.Headers.Names.RANGE
import mozilla.components.concept.fetch.MutableHeaders
import mozilla.components.concept.fetch.Request
import mozilla.components.concept.fetch.Response
import mozilla.components.feature.downloads.DownloadNotification.NOTIFICATION_DOWNLOAD_GROUP_ID
import mozilla.components.feature.downloads.ext.addCompletedDownload
import mozilla.components.feature.downloads.ext.isScheme
import mozilla.components.feature.downloads.ext.withResponse
import mozilla.components.feature.downloads.facts.emitNotificationCancelFact
import mozilla.components.feature.downloads.facts.emitNotificationOpenFact
import mozilla.components.feature.downloads.facts.emitNotificationPauseFact
import mozilla.components.feature.downloads.facts.emitNotificationResumeFact
import mozilla.components.feature.downloads.facts.emitNotificationTryAgainFact
import mozilla.components.feature.downloads.filewriter.DownloadFileWriter
import mozilla.components.support.base.android.NotificationsDelegate
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.kotlin.sanitizeURL
import mozilla.components.support.ktx.kotlinx.coroutines.throttleLatest
import mozilla.components.support.utils.DateTimeProvider
import mozilla.components.support.utils.DefaultDateTimeProvider
import mozilla.components.support.utils.DownloadFileUtils
import mozilla.components.support.utils.ext.registerReceiverCompat
import mozilla.components.support.utils.ext.stopForegroundCompat

/**
 * Service that performs downloads through a fetch [Client] rather than through the native Android download manager.
 *
 * To use this service, you must create a subclass in your application and add it to the manifest.
 */
@Suppress("LargeClass", "TooManyFunctions")
abstract class AbstractFetchDownloadService : Service() {
    private data class DownloadResponse(
        val response: Response,
        val usesHttpClient: Boolean,
    )

    private enum class ResumeResponseAction {
        CONTINUE,
        RESTART_FROM_BEGINNING,
        FAIL,
    }

    protected abstract val store: BrowserStore

    protected abstract val packageNameProvider: PackageNameProvider

    protected abstract val notificationsDelegate: NotificationsDelegate

    protected open val mainDispatcher: CoroutineDispatcher = Dispatchers.Main
    protected open val ioDispatcher: CoroutineDispatcher = Dispatchers.IO

    protected open val notificationUpdateScope by lazy { CoroutineScope(mainDispatcher + SupervisorJob()) }

    private val ioScope by lazy { CoroutineScope(ioDispatcher + SupervisorJob()) }

    protected abstract val httpClient: Client

    protected open val style: Style = Style()

    @VisibleForTesting
    internal open val context: Context
        get() = this

    @VisibleForTesting internal var compatForegroundNotificationId: Int = COMPAT_DEFAULT_FOREGROUND_ID
    private val logger = Logger("AbstractFetchDownloadService")

    internal var downloadJobs = mutableMapOf<String, DownloadJobState>()

    protected abstract val fileSizeFormatter: FileSizeFormatter
    protected abstract val downloadEstimator: DownloadEstimator

    protected abstract val downloadFileUtils: DownloadFileUtils

    protected abstract val downloadFileWriter: DownloadFileWriter

    protected open val dateTimeProvider: DateTimeProvider = DefaultDateTimeProvider()

    // TODO Move this to browser store and make immutable:
    // https://github.com/mozilla-mobile/android-components/issues/7050
    internal data class DownloadJobState(
        var job: Job? = null,
        @Volatile var state: DownloadState,
        var currentBytesCopied: Long = 0,
        @GuardedBy("context") var status: Status,
        var foregroundServiceId: Int = 0,
        var downloadDeleted: Boolean = false,
        var notifiedStopped: Boolean = false,
        var lastNotificationUpdate: Long = 0L,
        var dateTimeProvider: DateTimeProvider = DefaultDateTimeProvider(),
        var createdTime: Long = dateTimeProvider.currentTimeMillis(),
    ) {
        internal fun canUpdateNotification(): Boolean {
            return isUnderNotificationUpdateLimit() && !notifiedStopped
        }

        /**
         * Android imposes a limit on of how often we can send updates for a notification. The limit is one second per
         * update. See https://developer.android.com/training/notify-user/build-notification.html#Updating This function
         * indicates if we are under that limit.
         */
        internal fun isUnderNotificationUpdateLimit(): Boolean {
            return getSecondsSinceTheLastNotificationUpdate() >= 1
        }

        internal fun getSecondsSinceTheLastNotificationUpdate(): Long {
            return (dateTimeProvider.currentTimeMillis() - lastNotificationUpdate) / 1000
        }
    }

    internal fun setDownloadJobStatus(downloadJobState: DownloadJobState, status: Status) {
        synchronized(context) {
            if (status == DOWNLOADING) {
                downloadJobState.notifiedStopped = false
            }
            downloadJobState.status = status
            updateDownloadState(downloadJobState.state.copy(status = status))
        }
    }

    internal fun getDownloadJobStatus(downloadJobState: DownloadJobState): Status {
        synchronized(context) {
            return downloadJobState.status
        }
    }

    internal val broadcastReceiver by lazy {
        object : BroadcastReceiver() {
            @Suppress("LongMethod")
            override fun onReceive(context: Context, intent: Intent?) {
                val downloadId = intent?.extras?.getString(INTENT_EXTRA_DOWNLOAD_ID) ?: return
                val currentDownloadJobState = downloadJobs[downloadId] ?: return

                when (intent.action) {
                    ACTION_PAUSE -> {
                        setDownloadJobStatus(currentDownloadJobState, PAUSED)
                        currentDownloadJobState.job?.cancel()
                        emitNotificationPauseFact()
                        logger.debug("ACTION_PAUSE for ${currentDownloadJobState.state.id}")
                    }

                    ACTION_RESUME -> {
                        val fileExists =
                            downloadFileUtils.fileExists(
                                directoryPath = currentDownloadJobState.state.directoryPath,
                                fileName = currentDownloadJobState.state.fileName,
                            )
                        if (!fileExists) {
                            currentDownloadJobState.lastNotificationUpdate = dateTimeProvider.currentTimeMillis()
                            currentDownloadJobState.createdTime = dateTimeProvider.currentTimeMillis()
                            currentDownloadJobState.notifiedStopped = false

                            setDownloadJobStatus(currentDownloadJobState, FAILED)

                            updateDownloadNotification()
                        } else {
                            setDownloadJobStatus(currentDownloadJobState, DOWNLOADING)

                            currentDownloadJobState.job = ioScope.launch {
                                startDownloadJob(currentDownloadJobState)
                            }
                        }
                        emitNotificationResumeFact()
                        logger.debug("ACTION_RESUME for ${currentDownloadJobState.state.id}")
                    }

                    ACTION_CANCEL -> {
                        if (currentDownloadJobState.status !in listOf(COMPLETED, CANCELLED)) {
                            cancelDownloadJob(currentDownloadJobState)
                        }
                        removeDownloadJob(currentDownloadJobState)
                        emitNotificationCancelFact()
                        logger.debug("ACTION_CANCEL for ${currentDownloadJobState.state.id}")
                    }

                    ACTION_TRY_AGAIN -> {
                        removeNotification(context, currentDownloadJobState)
                        currentDownloadJobState.lastNotificationUpdate = dateTimeProvider.currentTimeMillis()
                        currentDownloadJobState.createdTime = dateTimeProvider.currentTimeMillis()
                        setDownloadJobStatus(currentDownloadJobState, DOWNLOADING)

                        currentDownloadJobState.job = ioScope.launch {
                            startDownloadJob(currentDownloadJobState)
                        }

                        emitNotificationTryAgainFact()
                        logger.debug("ACTION_TRY_AGAIN for ${currentDownloadJobState.state.id}")
                    }

                    ACTION_DISMISS -> {
                        removeDownloadJob(currentDownloadJobState)
                        logger.debug("ACTION_DISMISS for ${currentDownloadJobState.state.id}")
                    }

                    ACTION_OPEN -> {
                        if (
                            !downloadFileUtils.openFile(
                                fileName = currentDownloadJobState.state.fileName,
                                directoryPath = currentDownloadJobState.state.directoryPath,
                                contentType = currentDownloadJobState.state.contentType,
                            )
                        ) {
                            val fileExt = MimeTypeMap.getFileExtensionFromUrl(currentDownloadJobState.state.filePath)
                            val errorMessage =
                                applicationContext.getString(
                                    R.string.mozac_feature_downloads_open_not_supported1,
                                    fileExt,
                                )

                            Toast.makeText(applicationContext, errorMessage, Toast.LENGTH_SHORT).show()
                            logger.debug("ACTION_OPEN errorMessage for ${currentDownloadJobState.state.id} ")
                        }

                        emitNotificationOpenFact()
                        logger.debug("ACTION_OPEN for ${currentDownloadJobState.state.id}")
                    }
                }
            }
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        registerNotificationActionsReceiver()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val download =
            intent?.getStringExtra(EXTRA_DOWNLOAD_ID)?.let {
                store.state.downloads[it]
            } ?: return START_REDELIVER_INTENT

        when (intent.action) {
            ACTION_REMOVE_PRIVATE_DOWNLOAD -> {
                handleRemovePrivateDownloadIntent(download)
            }
            ACTION_TRY_AGAIN -> {
                val newDownloadState = download.copy(status = DOWNLOADING)
                store.dispatch(DownloadAction.UpdateDownloadAction(newDownloadState))
                handleDownloadIntent(newDownloadState)
            }

            else -> {
                handleDownloadIntent(download)
            }
        }

        return super.onStartCommand(intent, flags, startId)
    }

    @VisibleForTesting
    internal fun handleRemovePrivateDownloadIntent(download: DownloadState) {
        if (download.private) {
            downloadJobs[download.id]?.let {
                // Do not cancel already completed downloads.
                if (it.status != COMPLETED) {
                    cancelDownloadJob(it)
                }
                removeDownloadJob(it)
            }
            store.dispatch(DownloadAction.RemoveDownloadAction(download.id))
        }
    }

    @VisibleForTesting
    internal fun handleDownloadIntent(download: DownloadState) {
        // If the job already exists, then don't create a new ID. This can happen when calling tryAgain
        val foregroundServiceId = downloadJobs[download.id]?.foregroundServiceId ?: Random.nextInt()

        val actualStatus = if (download.status == INITIATED) DOWNLOADING else download.status

        // Create a new job and add it, with its downloadState to the map
        val downloadJobState =
            DownloadJobState(
                state = download.copy(status = actualStatus, notificationId = foregroundServiceId),
                foregroundServiceId = foregroundServiceId,
                status = actualStatus,
            )

        store.dispatch(DownloadAction.UpdateDownloadAction(downloadJobState.state))

        if (actualStatus == DOWNLOADING) {
            downloadJobState.job = ioScope.launch {
                startDownloadJob(downloadJobState)
            }
        }

        downloadJobs[download.id] = downloadJobState

        setForegroundNotification()

        notificationUpdateScope.launch {
            while (isActive) {
                delay(PROGRESS_UPDATE_INTERVAL)
                updateDownloadNotification()
                if (downloadJobs.isEmpty()) cancel()
            }
        }
    }

    @VisibleForTesting
    internal fun cancelDownloadJob(
        currentDownloadJobState: DownloadJobState,
        coroutineScope: CoroutineScope = CoroutineScope(ioDispatcher),
    ) {
        currentDownloadJobState.lastNotificationUpdate = dateTimeProvider.currentTimeMillis()
        setDownloadJobStatus(
            currentDownloadJobState,
            CANCELLED,
        )
        val resolver = context.contentResolver

        currentDownloadJobState.job?.cancel()
        currentDownloadJobState.job?.invokeOnCompletion {
            currentDownloadJobState.job = coroutineScope.launch {
                downloadFileUtils.deleteMediaFile(
                    contentResolver = resolver,
                    fileName = currentDownloadJobState.state.fileName,
                    directoryPath = currentDownloadJobState.state.directoryPath,
                )
                currentDownloadJobState.downloadDeleted = true
            }
        }
    }

    /**
     * Android rate limits notifications being sent, so we must send them on a delay so that notifications are not
     * dropped
     */
    private fun updateDownloadNotification() {
        for (download in downloadJobs.values) {
            if (!download.canUpdateNotification()) {
                continue
            }
            /*
             * We want to keep a consistent state in the UI, download.status can be changed from
             * another thread while we are posting updates to the UI, causing inconsistent UIs.
             * For this reason, we ONLY use the latest status during an UI update, new changes
             * will be posted in subsequent updates.
             */
            val uiStatus = getDownloadJobStatus(download)

            updateForegroundNotificationIfNeeded()

            // Dispatch the corresponding notification based on the current status
            updateDownloadNotification(uiStatus, download)

            if (uiStatus != DOWNLOADING) {
                sendDownloadStopped(download)
            }
        }
    }

    /**
     * Data class for styling download notifications.
     *
     * @param notificationAccentColor accent color for all download notifications.
     */
    data class Style(@param:ColorRes val notificationAccentColor: Int = R.color.mozac_feature_downloads_notification)

    /**
     * Updates the notification state with the passed [download] data. Be aware that you need to pass [latestUIStatus]
     * as [DownloadJobState.status] can be modified from another thread, causing inconsistencies in the ui.
     */
    @VisibleForTesting
    internal fun updateDownloadNotification(
        latestUIStatus: Status,
        download: DownloadJobState,
        scope: CoroutineScope = ioScope,
    ) {
        val notification =
            when (latestUIStatus) {
                DOWNLOADING ->
                    DownloadNotification.createOngoingDownloadNotification(
                        context = context,
                        downloadState = download.state,
                        fileSizeFormatter = fileSizeFormatter,
                        notificationAccentColor = style.notificationAccentColor,
                        downloadEstimator = downloadEstimator,
                    )
                PAUSED ->
                    DownloadNotification.createPausedDownloadNotification(
                        context,
                        download.state,
                        download.createdTime,
                        style.notificationAccentColor,
                    )
                FAILED ->
                    DownloadNotification.createDownloadFailedNotification(
                        context,
                        download.state,
                        download.createdTime,
                        style.notificationAccentColor,
                    )
                COMPLETED -> {
                    addToDownloadSystemDatabaseCompat(download.state, scope)
                    DownloadNotification.createDownloadCompletedNotification(
                        context = context,
                        downloadState = download.state,
                        createdTime = download.createdTime,
                        notificationAccentColor = style.notificationAccentColor,
                        downloadFileUtils = downloadFileUtils,
                    )
                }
                CANCELLED -> {
                    removeNotification(context, download)
                    download.lastNotificationUpdate = dateTimeProvider.currentTimeMillis()
                    null
                }
                INITIATED -> null
            }

        notification?.let {
            notificationsDelegate.notify(
                notificationId = download.foregroundServiceId,
                notification = it,
            )
            download.lastNotificationUpdate = dateTimeProvider.currentTimeMillis()
        }
    }

    override fun onTaskRemoved(rootIntent: Intent?) {
        stopSelf()
    }

    override fun onTimeout(startId: Int, fgsType: Int) {
        logger.error("Unable to finish download due to timeout")
        // calling stopSelf() will prevent the system throwing a RemoteServiceException
        stopSelf()
    }

    override fun onDestroy() {
        super.onDestroy()

        clearAllDownloadsNotificationsAndJobs()
        unregisterNotificationActionsReceiver()
    }

    // Cancels all running jobs and remove all notifications.
    // Also cleans any resources that we were holding like broadcastReceivers
    internal fun clearAllDownloadsNotificationsAndJobs() {
        val notificationManager = NotificationManagerCompat.from(context)

        stopForegroundCompat(true)
        compatForegroundNotificationId = COMPAT_DEFAULT_FOREGROUND_ID

        // Before doing any cleaning, we have to stop the notification updater scope.
        // To ensure we are not recreating the notifications.
        notificationUpdateScope.cancel()

        downloadJobs.values.forEach { state ->
            notificationManager.cancel(state.foregroundServiceId)
            if (state.status != COMPLETED && state.status != CANCELLED) {
                cancelDownloadJob(state)
            }
        }
        notificationManager.cancel(NOTIFICATION_DOWNLOAD_GROUP_ID)

        ioScope.cancel()
    }

    @Suppress("TooGenericExceptionCaught")
    internal fun startDownloadJob(currentDownloadJobState: DownloadJobState) {
        logger.debug("Starting download for ${currentDownloadJobState.state.id} ")
        try {
            performDownload(currentDownloadJobState)
        } catch (e: Exception) {
            logger.error("Unable to complete download for ${currentDownloadJobState.state.id} marked as FAILED", e)
            setDownloadJobStatus(currentDownloadJobState, FAILED)
        }
    }

    /**
     * Adds a file to the downloads database system, so it could appear in Downloads App (and thus become eligible for
     * management by the Downloads App) only for compatible devices otherwise nothing will happen.
     */
    @VisibleForTesting
    internal fun addToDownloadSystemDatabaseCompat(
        download: DownloadState,
        scope: CoroutineScope = ioScope,
    ) {
        if (!shouldUseScopedStorage()) {
            val fileName = download.fileName ?: throw IllegalStateException("A fileName for a download is required")
            val file = File(download.filePath)
            // addCompletedDownload can't handle any non http(s) urls
            scope.launch {
                addCompletedDownload(
                    title = fileName,
                    description = fileName,
                    isMediaScannerScannable = true,
                    mimeType =
                        downloadFileUtils.getSafeContentType(
                            fileName = download.fileName,
                            contentType = download.contentType,
                        ),
                    path = file.absolutePath,
                    length = download.contentLength ?: file.length(),
                    // Only show notifications if our channel is blocked
                    showNotification = !DownloadNotification.isChannelEnabled(context),
                    download,
                )
            }
        }
    }

    @VisibleForTesting
    @Suppress("LongParameterList")
    internal fun addCompletedDownload(
        title: String,
        description: String,
        isMediaScannerScannable: Boolean,
        mimeType: String,
        path: String,
        length: Long,
        showNotification: Boolean,
        download: DownloadState,
    ) {
        try {
            val url = if (!download.isScheme(listOf("http", "https"))) null else download.url.toUri()
            context.addCompletedDownload(
                title = title,
                description = description,
                isMediaScannerScannable = isMediaScannerScannable,
                mimeType = mimeType,
                path = path,
                length = length,
                // Only show notifications if our channel is blocked
                showNotification = showNotification,
                uri = url,
                referer = download.referrerUrl?.toUri(),
            )
        } catch (e: IllegalArgumentException) {
            logger.error("Unable add the download to the system database", e)
        }
    }

    @VisibleForTesting
    internal fun registerNotificationActionsReceiver() {
        val filter =
            IntentFilter().apply {
                addAction(ACTION_PAUSE)
                addAction(ACTION_RESUME)
                addAction(ACTION_CANCEL)
                addAction(ACTION_DISMISS)
                addAction(ACTION_TRY_AGAIN)
                addAction(ACTION_OPEN)
            }

        context.registerReceiverCompat(
            broadcastReceiver,
            filter,
            ContextCompat.RECEIVER_NOT_EXPORTED,
        )
    }

    @VisibleForTesting
    internal fun unregisterNotificationActionsReceiver() {
        context.unregisterReceiver(broadcastReceiver)
    }

    @VisibleForTesting
    internal fun removeDownloadJob(downloadJobState: DownloadJobState) {
        downloadJobs.remove(downloadJobState.state.id)
        if (downloadJobs.isEmpty()) {
            stopSelf()
        } else {
            updateForegroundNotificationIfNeeded()
            removeNotification(context, downloadJobState)
        }
    }

    @VisibleForTesting
    internal fun removeNotification(context: Context, currentDownloadJobState: DownloadJobState) {
        NotificationManagerCompat.from(context).cancel(currentDownloadJobState.foregroundServiceId)
    }

    /** Refresh the notification group content only for devices that support it, otherwise nothing will happen. */
    @VisibleForTesting
    internal fun updateNotificationGroup(): Notification {
        val downloadList = downloadJobs.values.toList()
        val notificationGroup =
            DownloadNotification.createDownloadGroupNotification(
                context = context,
                fileSizeFormatter = fileSizeFormatter,
                notifications = downloadList,
                notificationAccentColor = style.notificationAccentColor,
            )

        notificationsDelegate.notify(
            notificationId = NOTIFICATION_DOWNLOAD_GROUP_ID,
            notification = notificationGroup,
        )
        return notificationGroup
    }

    @VisibleForTesting internal fun getForegroundId(): Int = NOTIFICATION_DOWNLOAD_GROUP_ID

    /**
     * We create a separate notification which will be the foreground notification, it will be always present until we
     * don't have more active downloads.
     */
    @VisibleForTesting
    @Suppress("TooGenericExceptionCaught")
    internal fun setForegroundNotification() {
        val (notificationId, notification) = NOTIFICATION_DOWNLOAD_GROUP_ID to updateNotificationGroup()
        try {
            startForeground(notificationId, notification)
        } catch (e: Exception) {
            if (SDK_INT >= Build.VERSION_CODES.S && e is ForegroundServiceStartNotAllowedException) {
                // The app may no longer be allowed to start a foreground service, e.g. when it moved to the
                // background before this service was started (or when retrying a download from the background).
                // Keep running as a regular service instead of crashing: the download notification has already
                // been posted, and the download continues for as long as the system keeps the service alive.
                logger.error("Unable to start the download service in the foreground", e)
            } else {
                throw e
            }
        }
    }

    /**
     * Indicates the status of a download has changed and maybe the foreground notification needs, to be updated. For
     * devices that support group notifications, we update the overview notification
     */
    internal fun updateForegroundNotificationIfNeeded() {
        // This device supports notification groups, we just need to update the summary notification
        updateNotificationGroup()
        // If all downloads have been completed we don't need the status of
        // foreground service anymore, we can call stopForeground and let the user
        // swipe the foreground notification.
        val finishedDownloading = downloadJobs.values.toList().all { it.status == COMPLETED }
        if (finishedDownloading) {
            stopForegroundCompat(false)
        }
    }

    internal fun performDownload(currentDownloadJobState: DownloadJobState, useHttpClient: Boolean = false) {
        val download = currentDownloadJobState.state
        val isResumingDownload = currentDownloadJobState.currentBytesCopied > 0L

        val headers = buildDownloadRequestHeaders(currentDownloadJobState, download, isResumingDownload)
        if (headers == null) {
            verifyDownload(currentDownloadJobState)
            return
        }

        val (response, isUsingHttpClient) = fetchDownloadResponse(download, headers, isResumingDownload, useHttpClient)
        logger.debug("Fetching download for ${currentDownloadJobState.state.id} ")

        val resumeResponseAction =
            determineResumeResponseAction(
                isResumingDownload = isResumingDownload,
                response = response,
                expectedResumeStart = currentDownloadJobState.currentBytesCopied,
            )

        when (resumeResponseAction) {
            ResumeResponseAction.FAIL -> {
                failDownload(currentDownloadJobState, response)
            }
            ResumeResponseAction.RESTART_FROM_BEGINNING -> {
                restartDownloadFromBeginning(
                    currentDownloadJobState = currentDownloadJobState,
                    response = response,
                    usesHttpClient = isUsingHttpClient,
                )
            }
            ResumeResponseAction.CONTINUE -> {
                writeResponseBody(
                    currentDownloadJobState = currentDownloadJobState,
                    response = response,
                    append = isResumingDownload,
                    usesHttpClient = isUsingHttpClient,
                )
            }
        }
    }

    /** Updates the status of an ACTIVE download to completed or failed based on bytes copied */
    internal fun verifyDownload(download: DownloadJobState) {
        if (
            getDownloadJobStatus(download) == DOWNLOADING &&
                download.currentBytesCopied < (download.state.contentLength ?: 0)
        ) {
            setDownloadJobStatus(download, FAILED)
            logger.error("verifyDownload for ${download.state.id} FAILED")
        } else if (getDownloadJobStatus(download) == DOWNLOADING) {
            setDownloadJobStatus(download, COMPLETED)
            /**
             * In cases when we don't get the file size provided initially, we have to use
             * downloadState.currentBytesCopied as a fallback.
             */
            val fileSizeNotFound = download.state.contentLength == null || download.state.contentLength == 0L
            if (fileSizeNotFound) {
                val newState = download.state.copy(contentLength = download.currentBytesCopied)
                updateDownloadState(newState)
            }
            logger.debug("verifyDownload for ${download.state.id} ${download.status}")
        }
    }

    @VisibleForTesting
    internal enum class CopyInChuckStatus {
        COMPLETED,
        ERROR_IN_STREAM_CLOSED,
    }

    @VisibleForTesting
    internal fun copyInChunks(
        downloadJobState: DownloadJobState,
        inStream: InputStream,
        outStream: OutputStream,
        downloadWithHttpClient: Boolean = false,
    ): CopyInChuckStatus {
        val data = ByteArray(CHUNK_SIZE)
        logger.debug(
            "starting copyInChunks ${downloadJobState.state.id}" +
                " currentBytesCopied ${downloadJobState.state.currentBytesCopied}"
        )

        val throttleUpdateDownload =
            throttleLatest<Long>(
                PROGRESS_UPDATE_INTERVAL,
                coroutineScope = ioScope,
            ) { copiedBytes ->
                val newState = downloadJobState.state.copy(currentBytesCopied = copiedBytes)
                updateDownloadState(newState)
            }

        var isInStreamClosed = false
        // To ensure that we copy all files (even ones that don't have fileSize, we must NOT check < fileSize
        while (getDownloadJobStatus(downloadJobState) == DOWNLOADING) {
            var bytesRead: Int
            try {
                bytesRead = inStream.read(data)
            } catch (e: IOException) {
                if (downloadWithHttpClient) {
                    throw e
                }
                isInStreamClosed = true
                break
            }
            // If bytesRead is -1, there's no data left to read from the stream
            if (bytesRead == -1) {
                break
            }
            downloadJobState.currentBytesCopied += bytesRead

            throttleUpdateDownload(downloadJobState.currentBytesCopied)

            outStream.write(data, 0, bytesRead)
        }
        if (isInStreamClosed) {
            // In cases where [download.response] is available and users with slow
            // networks start a download but quickly press pause and then resume
            // [isResumingDownload] will be false as there will be not enough time
            // for bytes to be copied, but the stream in [download.response] will be closed,
            // we have to fallback to [httpClient]
            performDownload(downloadJobState, useHttpClient = true)
            return CopyInChuckStatus.ERROR_IN_STREAM_CLOSED
        }
        logger.debug(
            "Finishing copyInChunks ${downloadJobState.state.id} " +
                "currentBytesCopied ${downloadJobState.currentBytesCopied}"
        )
        return CopyInChuckStatus.COMPLETED
    }

    /**
     * Informs [mozilla.components.feature.downloads.manager.FetchDownloadManager] that a download is no longer in
     * progress due to being paused, completed, or failed
     */
    private fun sendDownloadStopped(downloadState: DownloadJobState) {
        downloadState.notifiedStopped = true

        val intent = Intent(ACTION_DOWNLOAD_COMPLETE)
        intent.putExtra(EXTRA_DOWNLOAD_STATUS, getDownloadJobStatus(downloadState))
        intent.putExtra(EXTRA_DOWNLOAD_ID, downloadState.state.id)
        intent.setPackage(packageNameProvider.packageName)

        context.sendBroadcast(
            intent,
            "${packageNameProvider.packageName}.permission.RECEIVE_DOWNLOAD_BROADCAST",
        )
    }

    @VisibleForTesting internal fun shouldUseScopedStorage() = getSdkVersion() >= Build.VERSION_CODES.Q

    /**
     * Gets the SDK version from the system. Used for testing since current robolectric version doesn't allow mocking
     * API 29, remove after update
     */
    @VisibleForTesting internal fun getSdkVersion(): Int = SDK_INT

    /** Updates the given [updatedDownload] in the store and in the [downloadJobs]. */
    @VisibleForTesting
    internal fun updateDownloadState(updatedDownload: DownloadState) {
        downloadJobs[updatedDownload.id]?.state = updatedDownload
        store.dispatch(DownloadAction.UpdateDownloadAction(updatedDownload))
    }

    private fun Response.hasExpectedResumeRange(expectedStart: Long): Boolean =
        parseContentRange(headers)?.start == expectedStart

    private fun buildDownloadRequestHeaders(
        currentDownloadJobState: DownloadJobState,
        download: DownloadState,
        isResumingDownload: Boolean,
    ): MutableHeaders? {
        if (!isResumingDownload) {
            return MutableHeaders()
        }

        logger.debug("Resuming download")
        if (currentDownloadJobState.currentBytesCopied == download.contentLength) {
            logger.debug("Already at 100%, verifying download")
            return null
        }

        return MutableHeaders(RANGE to "bytes=${currentDownloadJobState.currentBytesCopied}-")
    }

    private fun fetchDownloadResponse(
        download: DownloadState,
        headers: MutableHeaders,
        isResumingDownload: Boolean,
        useHttpClient: Boolean,
    ): DownloadResponse {
        val request =
            Request(
                url = download.url.sanitizeURL(),
                headers = headers,
                private = download.private,
                referrerUrl = download.referrerUrl,
            )

        if (isResumingDownload || useHttpClient || download.response == null) {
            return DownloadResponse(
                response = httpClient.fetch(request),
                usesHttpClient = true,
            )
        }

        return DownloadResponse(
            response = requireNotNull(download.response),
            usesHttpClient = false,
        )
    }

    private fun determineResumeResponseAction(
        isResumingDownload: Boolean,
        response: Response,
        expectedResumeStart: Long,
    ): ResumeResponseAction {
        val isAcceptedStatus = response.status == PARTIAL_CONTENT_STATUS || response.status == OK_STATUS
        if (!isAcceptedStatus) {
            return ResumeResponseAction.FAIL
        }

        if (!isResumingDownload) {
            return ResumeResponseAction.CONTINUE
        }

        return when {
            response.status == OK_STATUS -> ResumeResponseAction.RESTART_FROM_BEGINNING
            CONTENT_RANGE !in response.headers -> ResumeResponseAction.FAIL
            !response.hasExpectedResumeRange(expectedResumeStart) -> ResumeResponseAction.FAIL
            else -> ResumeResponseAction.CONTINUE
        }
    }

    private fun failDownload(currentDownloadJobState: DownloadJobState, response: Response) {
        response.close()
        // We experienced a problem trying to fetch the file, send a failure notification
        currentDownloadJobState.currentBytesCopied = 0
        currentDownloadJobState.state =
            currentDownloadJobState.state.copy(
                currentBytesCopied = 0,
                response = null,
            )
        setDownloadJobStatus(currentDownloadJobState, FAILED)
        logger.debug("Unable to fetch Download for ${currentDownloadJobState.state.id} status FAILED")
    }

    private fun restartDownloadFromBeginning(
        currentDownloadJobState: DownloadJobState,
        response: Response,
        usesHttpClient: Boolean,
    ) {
        logger.debug("Resume response ignored Range; restarting download from beginning")
        val partialFileExists =
            downloadFileUtils.fileExists(
                directoryPath = currentDownloadJobState.state.directoryPath,
                fileName = currentDownloadJobState.state.fileName,
            )
        if (partialFileExists) {
            val deleted =
                downloadFileUtils.deleteMediaFile(
                    contentResolver = context.contentResolver,
                    fileName = currentDownloadJobState.state.fileName,
                    directoryPath = currentDownloadJobState.state.directoryPath,
                )
            if (!deleted) {
                response.close()
                currentDownloadJobState.state = currentDownloadJobState.state.copy(response = null)
                setDownloadJobStatus(currentDownloadJobState, FAILED)
                logger.debug("Failed to delete partial download for ${currentDownloadJobState.state.id} status FAILED")
                return
            }
        }

        currentDownloadJobState.currentBytesCopied = 0
        currentDownloadJobState.state =
            currentDownloadJobState.state.copy(
                currentBytesCopied = 0,
                contentLength = null,
                response = null,
            )
        updateDownloadState(currentDownloadJobState.state)
        writeResponseBody(
            currentDownloadJobState = currentDownloadJobState,
            response = response,
            append = false,
            usesHttpClient = usesHttpClient,
        )
    }

    private fun writeResponseBody(
        currentDownloadJobState: DownloadJobState,
        response: Response,
        append: Boolean,
        usesHttpClient: Boolean,
    ) {
        response.body.useStream { inStream ->
            var copyInChunkStatus: CopyInChuckStatus? = null
            val newDownloadState =
                currentDownloadJobState.state.withResponse(
                    headers = response.headers,
                    downloadFileUtils = downloadFileUtils,
                    stream = inStream,
                )
            currentDownloadJobState.state = newDownloadState

            downloadFileWriter.useFileStream(
                download = newDownloadState,
                append = append,
                shouldUseScopedStorage = shouldUseScopedStorage(),
                onUpdateState = ::updateDownloadState,
                block = { outStream ->
                    copyInChunkStatus =
                        copyInChunks(
                            downloadJobState = currentDownloadJobState,
                            inStream = inStream,
                            outStream = outStream,
                            downloadWithHttpClient = usesHttpClient,
                        )
                },
            )

            if (copyInChunkStatus != CopyInChuckStatus.ERROR_IN_STREAM_CLOSED) {
                verifyDownload(currentDownloadJobState)
            }
        }
    }

    companion object {
        private const val CHUNK_SIZE = 32 * 1024
        private const val PARTIAL_CONTENT_STATUS = 206
        private const val OK_STATUS = 200

        /**
         * This interval was decided on by balancing the limit of the system (200ms) and allowing users to press buttons
         * on the notification. If a new notification is presented while a user is tapping a button, their press will be
         * cancelled.
         */
        internal const val PROGRESS_UPDATE_INTERVAL = 750L

        const val EXTRA_DOWNLOAD_STATUS = "mozilla.components.feature.downloads.extras.DOWNLOAD_STATUS"
        const val ACTION_OPEN = "mozilla.components.feature.downloads.OPEN"
        const val ACTION_PAUSE = "mozilla.components.feature.downloads.PAUSE"
        const val ACTION_RESUME = "mozilla.components.feature.downloads.RESUME"
        const val ACTION_CANCEL = "mozilla.components.feature.downloads.CANCEL"
        const val ACTION_DISMISS = "mozilla.components.feature.downloads.DISMISS"
        const val ACTION_REMOVE_PRIVATE_DOWNLOAD = "mozilla.components.feature.downloads.ACTION_REMOVE_PRIVATE_DOWNLOAD"
        const val ACTION_TRY_AGAIN = "mozilla.components.feature.downloads.TRY_AGAIN"
        const val COMPAT_DEFAULT_FOREGROUND_ID = -1
    }
}
