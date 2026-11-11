/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.downloads

import android.app.DownloadManager
import android.app.DownloadManager.EXTRA_DOWNLOAD_ID
import android.app.ForegroundServiceStartNotAllowedException
import android.app.Notification
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.FileProvider
import androidx.core.content.getSystemService
import androidx.core.net.toUri
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.time.LocalDate
import java.time.ZoneId
import kotlin.random.Random
import kotlin.test.assertNotNull
import kotlin.time.Duration.Companion.milliseconds
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestCoroutineScheduler
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.DownloadAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.content.DownloadState
import mozilla.components.browser.state.state.content.DownloadState.Status.CANCELLED
import mozilla.components.browser.state.state.content.DownloadState.Status.COMPLETED
import mozilla.components.browser.state.state.content.DownloadState.Status.DOWNLOADING
import mozilla.components.browser.state.state.content.DownloadState.Status.FAILED
import mozilla.components.browser.state.state.content.DownloadState.Status.INITIATED
import mozilla.components.browser.state.state.content.DownloadState.Status.PAUSED
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.fetch.Client
import mozilla.components.concept.fetch.MutableHeaders
import mozilla.components.concept.fetch.Request
import mozilla.components.concept.fetch.Response
import mozilla.components.feature.downloads.AbstractFetchDownloadService.Companion.ACTION_CANCEL
import mozilla.components.feature.downloads.AbstractFetchDownloadService.Companion.ACTION_PAUSE
import mozilla.components.feature.downloads.AbstractFetchDownloadService.Companion.ACTION_REMOVE_PRIVATE_DOWNLOAD
import mozilla.components.feature.downloads.AbstractFetchDownloadService.Companion.ACTION_RESUME
import mozilla.components.feature.downloads.AbstractFetchDownloadService.Companion.ACTION_TRY_AGAIN
import mozilla.components.feature.downloads.AbstractFetchDownloadService.Companion.PROGRESS_UPDATE_INTERVAL
import mozilla.components.feature.downloads.AbstractFetchDownloadService.CopyInChuckStatus.ERROR_IN_STREAM_CLOSED
import mozilla.components.feature.downloads.AbstractFetchDownloadService.DownloadJobState
import mozilla.components.feature.downloads.DownloadNotification.NOTIFICATION_DOWNLOAD_GROUP_ID
import mozilla.components.feature.downloads.facts.DownloadsFacts.Items.NOTIFICATION
import mozilla.components.feature.downloads.fake.FakeDownloadFileWriter
import mozilla.components.feature.downloads.fake.FakeFileSizeFormatter
import mozilla.components.feature.downloads.fake.FakePackageNameProvider
import mozilla.components.feature.downloads.filewriter.DownloadFileWriter
import mozilla.components.support.base.android.NotificationsDelegate
import mozilla.components.support.base.facts.Action
import mozilla.components.support.base.facts.processor.CollectionProcessor
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.eq
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.utils.DateTimeProvider
import mozilla.components.support.utils.DownloadFileUtils
import mozilla.components.support.utils.FakeDateTimeProvider
import mozilla.components.support.utils.FakeDownloadFileUtils
import mozilla.components.support.utils.ext.stopForegroundCompat
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import org.junit.runner.RunWith
import org.mockito.ArgumentMatchers.anyBoolean
import org.mockito.ArgumentMatchers.anyInt
import org.mockito.ArgumentMatchers.anyLong
import org.mockito.ArgumentMatchers.anyString
import org.mockito.ArgumentMatchers.argThat
import org.mockito.ArgumentMatchers.isNull
import org.mockito.Mock
import org.mockito.Mockito.atLeastOnce
import org.mockito.Mockito.clearInvocations
import org.mockito.Mockito.doAnswer
import org.mockito.Mockito.doNothing
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.doThrow
import org.mockito.Mockito.never
import org.mockito.Mockito.spy
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.mockito.Mockito.verifyNoInteractions
import org.mockito.MockitoAnnotations.openMocks
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import org.robolectric.annotation.Implementation
import org.robolectric.annotation.Implements
import org.robolectric.shadows.ShadowNotificationManager

@RunWith(AndroidJUnit4::class)
@Config(shadows = [ShadowFileProvider::class])
class AbstractFetchDownloadServiceTest {

    @Rule @JvmField val folder = TemporaryFolder()
    private val testDispatcher = StandardTestDispatcher(TestCoroutineScheduler())

    private val fakeFileSizeFormatter: FileSizeFormatter = FakeFileSizeFormatter()
    private val fakeDateTimeProvider: DateTimeProvider = FakeDateTimeProvider()
    private val fakeDownloadEstimator: DownloadEstimator = DownloadEstimator(fakeDateTimeProvider)

    private val fakePackageNameProvider: PackageNameProvider =
        FakePackageNameProvider("mozilla.components.feature.downloads.test")

    @Mock private lateinit var client: Client
    private lateinit var browserStore: BrowserStore
    private lateinit var notificationManagerCompat: NotificationManagerCompat

    private lateinit var notificationsDelegate: NotificationsDelegate

    private lateinit var shadowNotificationService: ShadowNotificationManager

    private val delayTime = PROGRESS_UPDATE_INTERVAL.milliseconds
    private val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()

    fun createService(
        browserStore: BrowserStore,
        testScope: CoroutineScope,
        scheduler: TestCoroutineScheduler,
        downloadFileUtils: DownloadFileUtils = FakeDownloadFileUtils(),
        downloadFileWriter: FakeDownloadFileWriter = FakeDownloadFileWriter(),
    ): AbstractFetchDownloadService =
        spy(
            object : AbstractFetchDownloadService() {
                override val httpClient = client
                override val store = browserStore
                override val notificationsDelegate = this@AbstractFetchDownloadServiceTest.notificationsDelegate
                override val fileSizeFormatter = fakeFileSizeFormatter
                override val downloadEstimator = fakeDownloadEstimator
                override val downloadFileUtils = downloadFileUtils
                override val packageNameProvider = fakePackageNameProvider
                override val context: Context = testContext
                override val downloadFileWriter: DownloadFileWriter = downloadFileWriter
                override val dateTimeProvider: DateTimeProvider = FakeDateTimeProvider(scheduler)
                override val mainDispatcher: CoroutineDispatcher = testDispatcher
                override val ioDispatcher: CoroutineDispatcher = testDispatcher
                override val notificationUpdateScope: CoroutineScope = testScope
            }
        )

    @Before
    fun setup() {
        openMocks(this)
        browserStore =
            BrowserStore(
                initialState = BrowserState(),
                middleware = listOf(captureActionsMiddleware),
            )
        notificationManagerCompat = spy(NotificationManagerCompat.from(testContext))
        notificationsDelegate = NotificationsDelegate(notificationManagerCompat)

        doReturn(true).`when`(notificationManagerCompat).areNotificationsEnabled()

        shadowNotificationService =
            shadowOf(testContext.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager)
    }

    @Test
    fun `begins download when started`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download = DownloadState("https://example.com/file.txt", "file.txt")
            val response =
                Response(
                    "https://example.com/file.txt",
                    200,
                    MutableHeaders(),
                    Response.Body(mock()),
                )
            doReturn(response).`when`(client).fetch(Request("https://example.com/file.txt"))

            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))
            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            val providedDownload = argumentCaptor<DownloadJobState>()
            verify(service).performDownload(providedDownload.capture(), anyBoolean())

            assertEquals(download.url, providedDownload.value.state.url)
            assertEquals(download.fileName, providedDownload.value.state.fileName)

            // Ensure the job is properly added to the map
            assertEquals(1, service.downloadJobs.count())
            assertNotNull(service.downloadJobs[providedDownload.value.state.id])
        }

    @Test
    fun `WHEN a download intent is received THEN handleDownloadIntent must be called`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)

            val download = DownloadState("https://example.com/file.txt", "file.txt")
            val downloadIntent = Intent("ACTION_DOWNLOAD")

            doNothing().`when`(service).handleRemovePrivateDownloadIntent(any())
            doNothing().`when`(service).handleDownloadIntent(any())

            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))

            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            verify(service).handleDownloadIntent(download)
            verify(service, never()).handleRemovePrivateDownloadIntent(download)
        }

    @Test
    fun `WHEN an intent does not provide an action THEN handleDownloadIntent must be called`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)

            val download = DownloadState("https://example.com/file.txt", "file.txt")
            val downloadIntent = Intent()

            doNothing().`when`(service).handleRemovePrivateDownloadIntent(any())
            doNothing().`when`(service).handleDownloadIntent(any())

            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))

            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            verify(service).handleDownloadIntent(download)
            verify(service, never()).handleRemovePrivateDownloadIntent(download)
        }

    @Test
    fun `WHEN a try again intent is received THEN handleDownloadIntent must be called`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)

            val download = DownloadState("https://example.com/file.txt", "file.txt")
            val downloadIntent = Intent(ACTION_TRY_AGAIN)

            doNothing().`when`(service).handleRemovePrivateDownloadIntent(any())
            doNothing().`when`(service).handleDownloadIntent(any())

            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)
            val newDownloadState = download.copy(status = DOWNLOADING)
            browserStore.dispatch(DownloadAction.AddDownloadAction(newDownloadState))

            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            verify(service).handleDownloadIntent(newDownloadState)
            assertEquals(newDownloadState.status, DOWNLOADING)
            verify(service, never()).handleRemovePrivateDownloadIntent(newDownloadState)
        }

    @Test
    fun `WHEN a remove download intent is received THEN handleRemoveDownloadIntent must be called`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)

            val download = DownloadState("https://example.com/file.txt", "file.txt")
            val downloadIntent = Intent(ACTION_REMOVE_PRIVATE_DOWNLOAD)

            doNothing().`when`(service).handleRemovePrivateDownloadIntent(any())
            doNothing().`when`(service).handleDownloadIntent(any())

            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))

            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            verify(service).handleRemovePrivateDownloadIntent(download)
            verify(service, never()).handleDownloadIntent(download)
        }

    @Test
    fun `WHEN handleRemovePrivateDownloadIntent with a private download is called THEN removeDownloadJob must be called`() =
        runTest(testDispatcher) {
            val downloadState = DownloadState(url = "mozilla.org/mozilla.txt", private = true)
            val downloadJobState = DownloadJobState(state = downloadState, status = COMPLETED)
            val service = createService(browserStore, backgroundScope, testScheduler)

            doAnswer {}.`when`(service).removeDownloadJob(any())

            service.downloadJobs[downloadState.id] = downloadJobState

            service.handleRemovePrivateDownloadIntent(downloadState)
            testDispatcher.scheduler.advanceUntilIdle()

            verify(service, times(0)).cancelDownloadJob(eq(downloadJobState), coroutineScope = any())
            verify(service).removeDownloadJob(downloadJobState)
            captureActionsMiddleware.assertFirstAction(DownloadAction.RemoveDownloadAction::class) { action ->
                assertEquals(downloadState.id, action.downloadId)
            }
        }

    @Test
    fun `WHEN handleRemovePrivateDownloadIntent is called with a private download AND not COMPLETED status THEN removeDownloadJob and cancelDownloadJob must be called`() =
        runTest(testDispatcher) {
            val downloadState = DownloadState(url = "mozilla.org/mozilla.txt", private = true)
            val downloadJobState = DownloadJobState(state = downloadState, status = DOWNLOADING)
            val service = createService(browserStore, backgroundScope, testScheduler)

            doAnswer {}.`when`(service).removeDownloadJob(any())

            service.downloadJobs[downloadState.id] = downloadJobState

            service.handleRemovePrivateDownloadIntent(downloadState)
            testDispatcher.scheduler.advanceUntilIdle()

            verify(service)
                .cancelDownloadJob(
                    currentDownloadJobState = eq(downloadJobState),
                    coroutineScope = any(),
                )
            verify(service).removeDownloadJob(downloadJobState)
            captureActionsMiddleware.assertFirstAction(DownloadAction.RemoveDownloadAction::class) { action ->
                assertEquals(downloadState.id, action.downloadId)
            }
        }

    @Test
    fun `WHEN handleRemovePrivateDownloadIntent is called with with a non-private (or regular) download THEN removeDownloadJob must not be called`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)

            val downloadState = DownloadState(url = "mozilla.org/mozilla.txt", private = false)
            val downloadJobState = DownloadJobState(state = downloadState, status = COMPLETED)

            doAnswer {}.`when`(service).removeDownloadJob(any())

            service.downloadJobs[downloadState.id] = downloadJobState

            service.handleRemovePrivateDownloadIntent(downloadState)
            testDispatcher.scheduler.advanceUntilIdle()

            verify(service, never()).removeDownloadJob(downloadJobState)
            captureActionsMiddleware.assertNotDispatched(DownloadAction.RemoveDownloadAction::class)
        }

    @Test
    fun `service redelivers if no download extra is passed `() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val downloadIntent = Intent("ACTION_DOWNLOAD")

            val intentCode = service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(Service.START_REDELIVER_INTENT, intentCode)
        }

    @Test
    fun `verifyDownload sets the download to failed if it is not complete`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val downloadState =
                DownloadState(
                    url = "mozilla.org/mozilla.txt",
                    contentLength = 50L,
                    currentBytesCopied = 5,
                    status = DOWNLOADING,
                )

            val downloadJobState =
                DownloadJobState(
                    job = null,
                    state = downloadState,
                    foregroundServiceId = 1,
                    downloadDeleted = false,
                    currentBytesCopied = 5,
                    status = DOWNLOADING,
                )

            service.verifyDownload(downloadJobState)

            assertEquals(FAILED, service.getDownloadJobStatus(downloadJobState))
            verify(service).setDownloadJobStatus(downloadJobState, FAILED)
            verify(service).updateDownloadState(downloadState.copy(status = FAILED))
        }

    @Test
    fun `verifyDownload does NOT set the download to failed if it is paused`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val downloadState =
                DownloadState(
                    url = "mozilla.org/mozilla.txt",
                    contentLength = 50L,
                    currentBytesCopied = 5,
                    status = PAUSED,
                )

            val downloadJobState =
                DownloadJobState(
                    job = null,
                    state = downloadState,
                    currentBytesCopied = 5,
                    status = PAUSED,
                    foregroundServiceId = 1,
                    downloadDeleted = false,
                )

            service.verifyDownload(downloadJobState)

            assertEquals(PAUSED, service.getDownloadJobStatus(downloadJobState))
            verify(service, times(0)).setDownloadJobStatus(downloadJobState, FAILED)
            verify(service, times(0)).updateDownloadState(downloadState.copy(status = FAILED))
        }

    @Test
    fun `verifyDownload does NOT set the download to failed if it is complete`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val downloadState =
                DownloadState(
                    url = "mozilla.org/mozilla.txt",
                    contentLength = 50L,
                    currentBytesCopied = 50,
                    status = DOWNLOADING,
                )

            val downloadJobState =
                DownloadJobState(
                    job = null,
                    state = downloadState,
                    currentBytesCopied = 50,
                    status = DOWNLOADING,
                    foregroundServiceId = 1,
                    downloadDeleted = false,
                )

            service.verifyDownload(downloadJobState)

            assertNotEquals(FAILED, service.getDownloadJobStatus(downloadJobState))
            verify(service, times(0)).setDownloadJobStatus(downloadJobState, FAILED)
            verify(service, times(0)).updateDownloadState(downloadState.copy(status = FAILED))
        }

    @Test
    fun `verifyDownload does NOT set the download to failed if it is cancelled`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val downloadState =
                DownloadState(
                    url = "mozilla.org/mozilla.txt",
                    contentLength = 50L,
                    currentBytesCopied = 50,
                    status = CANCELLED,
                )

            val downloadJobState =
                DownloadJobState(
                    job = null,
                    state = downloadState,
                    currentBytesCopied = 50,
                    status = CANCELLED,
                    foregroundServiceId = 1,
                    downloadDeleted = false,
                )

            service.verifyDownload(downloadJobState)

            assertNotEquals(FAILED, service.getDownloadJobStatus(downloadJobState))
            verify(service, times(0)).setDownloadJobStatus(downloadJobState, FAILED)
            verify(service, times(0)).updateDownloadState(downloadState.copy(status = FAILED))
        }

    @Test
    fun `verifyDownload does NOT set the download to failed if it is status COMPLETED`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val downloadState =
                DownloadState(
                    url = "mozilla.org/mozilla.txt",
                    contentLength = 50L,
                    currentBytesCopied = 50,
                    status = COMPLETED,
                )

            val downloadJobState =
                DownloadJobState(
                    job = null,
                    state = downloadState,
                    currentBytesCopied = 50,
                    status = COMPLETED,
                    foregroundServiceId = 1,
                    downloadDeleted = false,
                )

            service.verifyDownload(downloadJobState)

            verify(service, times(0)).setDownloadJobStatus(downloadJobState, FAILED)
            verify(service, times(0)).updateDownloadState(downloadState.copy(status = FAILED))
        }

    @Test
    fun `verify that a COMPLETED download contains a file size`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val downloadState =
                DownloadState(
                    url = "mozilla.org/mozilla.txt",
                    contentLength = 0L,
                    currentBytesCopied = 50,
                    status = DOWNLOADING,
                )
            val downloadJobState =
                DownloadJobState(
                    job = null,
                    state = downloadState,
                    currentBytesCopied = 50,
                    status = DOWNLOADING,
                    foregroundServiceId = 1,
                    downloadDeleted = false,
                )

            browserStore.dispatch(DownloadAction.AddDownloadAction(downloadState))
            service.downloadJobs[downloadJobState.state.id] = downloadJobState
            service.verifyDownload(downloadJobState)

            assertEquals(
                downloadJobState.state.contentLength,
                service.downloadJobs[downloadJobState.state.id]!!.state.contentLength,
            )
            assertEquals(
                downloadJobState.state.contentLength,
                browserStore.state.downloads.values.first().contentLength,
            )
        }

    @Test
    fun `broadcastReceiver handles ACTION_PAUSE`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download = DownloadState("https://example.com/file.txt", "file.txt")
            val response =
                Response(
                    "https://example.com/file.txt",
                    200,
                    MutableHeaders(),
                    Response.Body(mock()),
                )
            doReturn(response).`when`(client).fetch(Request("https://example.com/file.txt"))

            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))
            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            val providedDownload = argumentCaptor<DownloadJobState>()
            verify(service).performDownload(providedDownload.capture(), anyBoolean())

            val pauseIntent =
                Intent(ACTION_PAUSE).apply {
                    setPackage(testContext.applicationContext.packageName)
                    putExtra(INTENT_EXTRA_DOWNLOAD_ID, providedDownload.value.state.id)
                }

            CollectionProcessor.withFactCollection { facts ->
                service.broadcastReceiver.onReceive(testContext, pauseIntent)

                val pauseFact = facts[0]
                assertEquals(Action.PAUSE, pauseFact.action)
                assertEquals(NOTIFICATION, pauseFact.item)
            }

            val downloadJobState = service.downloadJobs[providedDownload.value.state.id]!!
            assertEquals(PAUSED, service.getDownloadJobStatus(downloadJobState))
        }

    @Test
    fun `broadcastReceiver handles ACTION_CANCEL`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download = DownloadState("https://example.com/file.txt", "file.txt")
            val response =
                Response(
                    "https://example.com/file.txt",
                    200,
                    MutableHeaders(),
                    Response.Body(mock()),
                )
            doReturn(response).`when`(client).fetch(Request("https://example.com/file.txt"))

            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))
            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            val providedDownload = argumentCaptor<DownloadJobState>()
            verify(service).performDownload(providedDownload.capture(), anyBoolean())

            val cancelIntent =
                Intent(ACTION_CANCEL).apply {
                    setPackage(testContext.applicationContext.packageName)
                    putExtra(INTENT_EXTRA_DOWNLOAD_ID, providedDownload.value.state.id)
                }

            assertFalse(service.downloadJobs[providedDownload.value.state.id]!!.downloadDeleted)

            CollectionProcessor.withFactCollection { facts ->
                service.broadcastReceiver.onReceive(testContext, cancelIntent)

                val cancelFact = facts[0]
                assertEquals(Action.CANCEL, cancelFact.action)
                assertEquals(NOTIFICATION, cancelFact.item)
            }
        }

    @Test
    fun `WHEN ACTION_CANCEL is received for a COMPLETED download THEN file is not deleted`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download = DownloadState("https://example.com/file.txt", "file.txt")
            val response =
                Response(
                    "https://example.com/file.txt",
                    200,
                    MutableHeaders(),
                    Response.Body(mock()),
                )
            doReturn(response).`when`(client).fetch(Request("https://example.com/file.txt"))

            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))
            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            val providedDownload = argumentCaptor<DownloadJobState>()
            verify(service).performDownload(providedDownload.capture(), anyBoolean())

            val downloadJobState = service.downloadJobs[providedDownload.value.state.id]!!
            service.setDownloadJobStatus(downloadJobState, COMPLETED)

            val cancelIntent =
                Intent(ACTION_CANCEL).apply {
                    setPackage(testContext.applicationContext.packageName)
                    putExtra(INTENT_EXTRA_DOWNLOAD_ID, downloadJobState.state.id)
                }

            service.broadcastReceiver.onReceive(testContext, cancelIntent)
            testDispatcher.scheduler.advanceUntilIdle()

            assertFalse(downloadJobState.downloadDeleted)
            assertEquals(COMPLETED, service.getDownloadJobStatus(downloadJobState))
            assertNull(service.downloadJobs[downloadJobState.state.id])
        }

    @Test
    fun `WHEN ACTION_CANCEL is received for a CANCELLED download THEN file is not deleted`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download = DownloadState("https://example.com/file.txt", "file.txt")
            val response =
                Response(
                    "https://example.com/file.txt",
                    200,
                    MutableHeaders(),
                    Response.Body(mock()),
                )
            doReturn(response).`when`(client).fetch(Request("https://example.com/file.txt"))

            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))
            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            val providedDownload = argumentCaptor<DownloadJobState>()
            verify(service).performDownload(providedDownload.capture(), anyBoolean())

            val downloadJobState = service.downloadJobs[providedDownload.value.state.id]!!
            service.setDownloadJobStatus(downloadJobState, CANCELLED)

            val cancelIntent =
                Intent(ACTION_CANCEL).apply {
                    setPackage(testContext.applicationContext.packageName)
                    putExtra(INTENT_EXTRA_DOWNLOAD_ID, downloadJobState.state.id)
                }

            service.broadcastReceiver.onReceive(testContext, cancelIntent)
            testDispatcher.scheduler.advanceUntilIdle()

            assertFalse(downloadJobState.downloadDeleted)
            assertEquals(CANCELLED, service.getDownloadJobStatus(downloadJobState))
            assertNull(service.downloadJobs[downloadJobState.state.id])
        }

    @Test
    fun `WHEN ACTION_CANCEL is received for a DOWNLOADING download THEN the state of the download is CANCELED`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download = DownloadState("https://example.com/file.txt", "file.txt", status = DOWNLOADING)
            val downloadJobState = DownloadJobState(state = download, status = DOWNLOADING)

            service.downloadJobs[download.id] = downloadJobState
            assertEquals(DOWNLOADING, service.getDownloadJobStatus(downloadJobState))

            val cancelIntent =
                Intent(ACTION_CANCEL).apply {
                    setPackage(testContext.applicationContext.packageName)
                    putExtra(INTENT_EXTRA_DOWNLOAD_ID, downloadJobState.state.id)
                }

            service.broadcastReceiver.onReceive(testContext, cancelIntent)
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(CANCELLED, service.getDownloadJobStatus(downloadJobState))
            assertNull(service.downloadJobs[downloadJobState.state.id])
        }

    @Test
    fun `WHEN ACTION_CANCEL is received for a FAILED download THEN file is deleted`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download = DownloadState("https://example.com/file.txt", "file.txt")
            val response =
                Response(
                    "https://example.com/file.txt",
                    200,
                    MutableHeaders(),
                    Response.Body(mock()),
                )
            doReturn(response).`when`(client).fetch(Request("https://example.com/file.txt"))

            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))
            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            val providedDownload = argumentCaptor<DownloadJobState>()
            verify(service).performDownload(providedDownload.capture(), anyBoolean())

            val downloadJobState = service.downloadJobs[providedDownload.value.state.id]!!
            service.setDownloadJobStatus(downloadJobState, FAILED)

            val cancelIntent =
                Intent(ACTION_CANCEL).apply {
                    setPackage(testContext.applicationContext.packageName)
                    putExtra(INTENT_EXTRA_DOWNLOAD_ID, downloadJobState.state.id)
                }

            service.broadcastReceiver.onReceive(testContext, cancelIntent)
            testDispatcher.scheduler.advanceUntilIdle()

            assertTrue(downloadJobState.downloadDeleted)
            assertNull(service.downloadJobs[downloadJobState.state.id])
        }

    @Test
    fun `WHEN an intent is sent with an ACTION_RESUME action and the file exists THEN the broadcastReceiver resumes the download`() =
        runTest(testDispatcher) {
            val service =
                createService(
                    browserStore = browserStore,
                    testScope = backgroundScope,
                    scheduler = testScheduler,
                    downloadFileUtils = FakeDownloadFileUtils(fileExists = { _, _ -> true }),
                )
            doReturn(AbstractFetchDownloadService.CopyInChuckStatus.COMPLETED)
                .`when`(service)
                .copyInChunks(
                    any(),
                    any(),
                    any(),
                    anyBoolean(),
                )

            folder.newFile("file.txt")

            val download =
                DownloadState(
                    url = "https://example.com/file.txt",
                    fileName = "file.txt",
                )
            val downloadResponse =
                Response(
                    "https://example.com/file.txt",
                    200,
                    MutableHeaders(),
                    Response.Body(mock()),
                )
            val resumeResponse =
                Response(
                    "https://example.com/file.txt",
                    206,
                    MutableHeaders("Content-Range" to "bytes 1-67588/67589"),
                    Response.Body(mock()),
                )
            doReturn(downloadResponse).`when`(client).fetch(Request("https://example.com/file.txt"))
            doReturn(resumeResponse)
                .`when`(client)
                .fetch(
                    Request(
                        "https://example.com/file.txt",
                        headers = MutableHeaders("Range" to "bytes=1-"),
                    )
                )

            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))
            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            val providedDownload = argumentCaptor<DownloadJobState>()
            verify(service).performDownload(providedDownload.capture(), anyBoolean())

            // Simulate a pause
            var downloadJobState = service.downloadJobs[providedDownload.value.state.id]!!
            downloadJobState.currentBytesCopied = 1
            service.setDownloadJobStatus(downloadJobState, PAUSED)

            service.setDownloadJobStatus(downloadJobState, PAUSED)
            service.downloadJobs[providedDownload.value.state.id]?.job?.cancel()

            val resumeIntent =
                Intent(ACTION_RESUME).apply {
                    setPackage(testContext.applicationContext.packageName)
                    putExtra(INTENT_EXTRA_DOWNLOAD_ID, providedDownload.value.state.id)
                }

            CollectionProcessor.withFactCollection { facts ->
                service.broadcastReceiver.onReceive(testContext, resumeIntent)
                testDispatcher.scheduler.advanceUntilIdle()

                val resumeFact = facts[0]
                assertEquals(Action.RESUME, resumeFact.action)
                assertEquals(NOTIFICATION, resumeFact.item)
            }

            downloadJobState = service.downloadJobs[providedDownload.value.state.id]!!
            assertEquals(COMPLETED, service.getDownloadJobStatus(downloadJobState))

            // Make sure the download job is completed (break out of copyInChunks)
            service.setDownloadJobStatus(downloadJobState, PAUSED)

            verify(service).startDownloadJob(providedDownload.value)

            File(downloadJobState.state.filePath).delete()
        }

    @Test
    fun `WHEN an intent is sent with an ACTION_RESUME action and the file doesn't exist THEN the broadcastReceiver sets the download status to FAILED`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            folder.newFile("file.txt")

            val download =
                DownloadState(
                    url = "https://example.com/file.txt",
                    fileName = "file.txt",
                    directoryPath = folder.root.path,
                )

            val downloadResponse =
                Response(
                    "https://example.com/file.txt",
                    200,
                    MutableHeaders(),
                    Response.Body(mock()),
                )
            val resumeResponse =
                Response(
                    "https://example.com/file.txt",
                    206,
                    MutableHeaders("Content-Range" to "1-67589/67589"),
                    Response.Body(mock()),
                )

            doReturn(downloadResponse).`when`(client).fetch(Request("https://example.com/file.txt"))
            doReturn(resumeResponse)
                .`when`(client)
                .fetch(
                    Request(
                        "https://example.com/file.txt",
                        headers = MutableHeaders("Range" to "bytes=1-"),
                    )
                )

            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))
            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            val providedDownload = argumentCaptor<DownloadJobState>()
            verify(service).performDownload(providedDownload.capture(), anyBoolean())

            // Simulate a pause
            var downloadJobState = service.downloadJobs[providedDownload.value.state.id]!!
            downloadJobState.currentBytesCopied = 1
            service.setDownloadJobStatus(downloadJobState, PAUSED)

            File(downloadJobState.state.filePath).delete()

            val resumeIntent =
                Intent(ACTION_RESUME).apply {
                    setPackage(testContext.applicationContext.packageName)
                    putExtra(INTENT_EXTRA_DOWNLOAD_ID, providedDownload.value.state.id)
                }

            doNothing().`when`(service).updateDownloadNotification(any(), any(), any())

            CollectionProcessor.withFactCollection { facts ->
                service.broadcastReceiver.onReceive(testContext, resumeIntent)

                val resumeFact = facts[0]
                assertEquals(Action.RESUME, resumeFact.action)
                assertEquals(NOTIFICATION, resumeFact.item)
            }

            downloadJobState = service.downloadJobs[providedDownload.value.state.id]!!

            assertEquals(FAILED, service.getDownloadJobStatus(downloadJobState))
        }

    @Test
    fun `broadcastReceiver handles ACTION_TRY_AGAIN`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download = DownloadState("https://example.com/file.txt", "file.txt", contentLength = 1000)
            val response =
                Response(
                    "https://example.com/file.txt",
                    200,
                    MutableHeaders(),
                    Response.Body(mock()),
                )
            doReturn(response).`when`(client).fetch(Request("https://example.com/file.txt"))

            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))
            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            val providedDownload = argumentCaptor<DownloadJobState>()
            verify(service).performDownload(providedDownload.capture(), anyBoolean())

            // Simulate a failure
            var downloadJobState = service.downloadJobs[providedDownload.value.state.id]!!
            service.setDownloadJobStatus(downloadJobState, FAILED)
            service.downloadJobs[providedDownload.value.state.id]?.job?.cancel()

            val tryAgainIntent =
                Intent(ACTION_TRY_AGAIN).apply {
                    setPackage(testContext.applicationContext.packageName)
                    putExtra(INTENT_EXTRA_DOWNLOAD_ID, providedDownload.value.state.id)
                }

            CollectionProcessor.withFactCollection { facts ->
                service.broadcastReceiver.onReceive(testContext, tryAgainIntent)

                val tryAgainFact = facts[0]
                assertEquals(Action.TRY_AGAIN, tryAgainFact.action)
                assertEquals(NOTIFICATION, tryAgainFact.item)
            }

            downloadJobState = service.downloadJobs[providedDownload.value.state.id]!!
            assertEquals(DOWNLOADING, service.getDownloadJobStatus(downloadJobState))

            // Make sure the download job is completed (break out of copyInChunks)
            service.setDownloadJobStatus(downloadJobState, PAUSED)

            verify(service).startDownloadJob(providedDownload.value)
        }

    @Test
    fun `download fails on a bad network response`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download = DownloadState("https://example.com/file.txt", "file.txt")
            val response =
                Response(
                    "https://example.com/file.txt",
                    400,
                    MutableHeaders(),
                    Response.Body(mock()),
                )
            doReturn(response).`when`(client).fetch(Request("https://example.com/file.txt"))

            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))
            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            val providedDownload = argumentCaptor<DownloadJobState>()
            verify(service).performDownload(providedDownload.capture(), anyBoolean())

            val downloadJobState = service.downloadJobs[providedDownload.value.state.id]!!
            assertEquals(FAILED, service.getDownloadJobStatus(downloadJobState))
        }

    @Test
    fun `notification is shown when download status is ACTIVE`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download = DownloadState("https://example.com/file.txt", "file.txt")
            val response =
                Response(
                    "https://example.com/file.txt",
                    200,
                    MutableHeaders(),
                    Response.Body(mock()),
                )
            doReturn(response).`when`(client).fetch(Request("https://example.com/file.txt"))

            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))
            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            val providedDownload = argumentCaptor<DownloadJobState>()
            verify(service).performDownload(providedDownload.capture(), anyBoolean())

            val downloadJobState = service.downloadJobs[providedDownload.value.state.id]!!
            service.setDownloadJobStatus(downloadJobState, DOWNLOADING)
            assertEquals(DOWNLOADING, service.getDownloadJobStatus(downloadJobState))

            testDispatcher.scheduler.advanceTimeBy(delayTime)
            testDispatcher.scheduler.runCurrent()

            // The additional notification is the summary one (the notification group).
            assertEquals(2, shadowNotificationService.size())
        }

    @Test
    fun `WHEN a failed download is tried again, created time is updated`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val fakeClock = FakeDateTimeProvider(testScheduler)

            val downloadId = "cakes"
            val downloadJobState =
                DownloadJobState(
                    state = DownloadState(url = "", id = downloadId),
                    status = FAILED,
                    dateTimeProvider = fakeClock,
                    createdTime = 0,
                )
            service.downloadJobs[downloadId] = downloadJobState
            testScheduler.advanceTimeBy(1000.milliseconds)

            val tryAgainIntent =
                Intent(ACTION_TRY_AGAIN).apply {
                    putExtra(INTENT_EXTRA_DOWNLOAD_ID, downloadId)
                }
            service.broadcastReceiver.onReceive(testContext, tryAgainIntent)
            assertTrue(downloadJobState.createdTime > 0)
        }

    @Test
    fun `onStartCommand must change status of INITIATED downloads to DOWNLOADING`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download = DownloadState("https://example.com/file.txt", "file.txt", status = INITIATED)

            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            doNothing().`when`(service).performDownload(any(), anyBoolean())

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))
            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            service.downloadJobs.values.first().job!!

            verify(service).startDownloadJob(any())
            assertEquals(DOWNLOADING, service.downloadJobs.values.first().status)
        }

    @Test
    fun `onStartCommand must change the status only for INITIATED downloads`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download = DownloadState("https://example.com/file.txt", "file.txt", status = FAILED)

            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))
            service.onStartCommand(downloadIntent, 0, 0)

            verify(service, never()).startDownloadJob(any())
            assertEquals(FAILED, service.downloadJobs.values.first().status)
        }

    @Test
    fun `onStartCommand sets the notification foreground`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download = DownloadState("https://example.com/file.txt", "file.txt")

            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            doNothing().`when`(service).performDownload(any(), anyBoolean())

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))
            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            verify(service).setForegroundNotification()
        }

    @Test
    fun `sets the notification foreground in devices that support notification group`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download =
                DownloadState(
                    id = "1",
                    url = "https://example.com/file.txt",
                    fileName = "file.txt",
                    status = DOWNLOADING,
                )
            val downloadState =
                DownloadJobState(
                    state = download,
                    status = DOWNLOADING,
                    foregroundServiceId = Random.nextInt(),
                )
            val notification = mock<Notification>()

            doReturn(notification).`when`(service).updateNotificationGroup()

            service.downloadJobs["1"] = downloadState

            service.setForegroundNotification()

            verify(service).startForeground(NOTIFICATION_DOWNLOAD_GROUP_ID, notification)
        }

    @Test
    @Config(sdk = [31])
    fun `onStartCommand does not crash when the app is not allowed to start a foreground service`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download = DownloadState("https://example.com/file.txt", "file.txt")

            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            doNothing().`when`(service).performDownload(any(), anyBoolean())
            doThrow(ForegroundServiceStartNotAllowedException("Test thrown exception"))
                .`when`(service)
                .startForeground(anyInt(), any())

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))
            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            verify(service).startForeground(eq(NOTIFICATION_DOWNLOAD_GROUP_ID), any())
            verify(service).startDownloadJob(any())
            assertEquals(DOWNLOADING, service.downloadJobs[download.id]?.status)
        }

    @Test
    @Config(sdk = [30])
    fun `setForegroundNotification rethrows exceptions other than ForegroundServiceStartNotAllowedException`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val exception = IllegalStateException("Test thrown exception")
            doReturn(mock<Notification>()).`when`(service).updateNotificationGroup()
            doThrow(exception).`when`(service).startForeground(anyInt(), any())

            val thrown = assertThrows(IllegalStateException::class.java) { service.setForegroundNotification() }
            assertEquals(exception, thrown)
        }

    @Test
    fun `getForegroundId in devices that support notification group will return NOTIFICATION_DOWNLOAD_GROUP_ID`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download = DownloadState(id = "1", url = "https://example.com/file.txt", fileName = "file.txt")

            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            doNothing().`when`(service).performDownload(any(), anyBoolean())

            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(NOTIFICATION_DOWNLOAD_GROUP_ID, service.getForegroundId())
        }

    @Test
    fun `removeDownloadJob will update the background notification if there are other pending downloads`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download =
                DownloadState(
                    id = "1",
                    url = "https://example.com/file.txt",
                    fileName = "file.txt",
                    status = DOWNLOADING,
                )
            val downloadState =
                DownloadJobState(
                    state = download,
                    status = DOWNLOADING,
                    foregroundServiceId = Random.nextInt(),
                )

            service.downloadJobs["1"] = downloadState
            service.downloadJobs["2"] = mock()

            doNothing().`when`(service).updateForegroundNotificationIfNeeded()

            service.removeDownloadJob(downloadJobState = downloadState)

            assertEquals(1, service.downloadJobs.size)
            verify(service).updateForegroundNotificationIfNeeded()
            verify(service).removeNotification(testContext, downloadState)
        }

    @Test
    fun `WHEN all downloads are completed stopForeground must be called`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download1 =
                DownloadState(
                    id = "1",
                    url = "https://example.com/file1.txt",
                    fileName = "file1.txt",
                    status = COMPLETED,
                )
            val download2 =
                DownloadState(
                    id = "2",
                    url = "https://example.com/file2.txt",
                    fileName = "file2.txt",
                    status = COMPLETED,
                )
            val downloadState1 =
                DownloadJobState(
                    state = download1,
                    status = COMPLETED,
                    foregroundServiceId = Random.nextInt(),
                )

            val downloadState2 =
                DownloadJobState(
                    state = download2,
                    status = COMPLETED,
                    foregroundServiceId = Random.nextInt(),
                )

            service.downloadJobs["1"] = downloadState1
            service.downloadJobs["2"] = downloadState2

            service.updateForegroundNotificationIfNeeded()

            verify(service).stopForegroundCompat(false)
        }

    @Test
    fun `Until all downloads are NOT completed stopForeground must NOT be called`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download1 =
                DownloadState(
                    id = "1",
                    url = "https://example.com/file1.txt",
                    fileName = "file1.txt",
                    status = COMPLETED,
                )
            val download2 =
                DownloadState(
                    id = "2",
                    url = "https://example.com/file2.txt",
                    fileName = "file2.txt",
                    status = DOWNLOADING,
                )
            val downloadState1 =
                DownloadJobState(
                    state = download1,
                    status = COMPLETED,
                    foregroundServiceId = Random.nextInt(),
                )

            val downloadState2 =
                DownloadJobState(
                    state = download2,
                    status = DOWNLOADING,
                    foregroundServiceId = Random.nextInt(),
                )

            service.downloadJobs["1"] = downloadState1
            service.downloadJobs["2"] = downloadState2

            service.updateForegroundNotificationIfNeeded()

            verify(service, never()).stopForeground(Service.STOP_FOREGROUND_DETACH)
        }

    @Test
    fun `removeDownloadJob will stop the service if there are none pending downloads`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download =
                DownloadState(
                    id = "1",
                    url = "https://example.com/file.txt",
                    fileName = "file.txt",
                    status = DOWNLOADING,
                )
            val downloadState =
                DownloadJobState(
                    state = download,
                    status = DOWNLOADING,
                    foregroundServiceId = Random.nextInt(),
                )

            doNothing().`when`(service).stopForeground(Service.STOP_FOREGROUND_DETACH)
            doNothing().`when`(service).clearAllDownloadsNotificationsAndJobs()
            doNothing().`when`(service).stopSelf()

            service.downloadJobs["1"] = downloadState

            service.removeDownloadJob(downloadJobState = downloadState)

            assertTrue(service.downloadJobs.isEmpty())
            verify(service).stopSelf()
            verify(service, times(0)).updateForegroundNotificationIfNeeded()
        }

    @Test
    fun `updateForegroundNotification will update the notification group for devices that support it`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            doReturn(null).`when`(service).updateNotificationGroup()

            service.updateForegroundNotificationIfNeeded()

            verify(service).updateNotificationGroup()
        }

    @Test
    fun `notification is shown when download status is PAUSED`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download = DownloadState("https://example.com/file.txt", "file.txt")
            val response =
                Response(
                    "https://example.com/file.txt",
                    200,
                    MutableHeaders(),
                    Response.Body(mock()),
                )
            doReturn(response).`when`(client).fetch(Request("https://example.com/file.txt"))

            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))
            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            val providedDownload = argumentCaptor<DownloadJobState>()
            verify(service).performDownload(providedDownload.capture(), anyBoolean())

            val downloadJobState = service.downloadJobs[providedDownload.value.state.id]!!
            service.setDownloadJobStatus(downloadJobState, PAUSED)
            assertEquals(PAUSED, service.getDownloadJobStatus(downloadJobState))

            testDispatcher.scheduler.advanceTimeBy(delayTime)
            testDispatcher.scheduler.runCurrent()

            // one of the notifications it is the group notification only for devices the support it
            assertEquals(2, shadowNotificationService.size())
        }

    @Test
    fun `notification is shown when download status is COMPLETED`() =
        runTest(testDispatcher) {
            performSuccessfulCompleteDownload(backgroundScope, testScheduler)
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(2, shadowNotificationService.size())
        }

    @Test
    fun `completed download notification avoids notification trampoline restrictions by using an activity based PendingIntent to open the file`() =
        runTest(testDispatcher) {
            val downloadJobState = performSuccessfulCompleteDownload(backgroundScope, testScheduler)

            val notification = shadowNotificationService.getNotification(downloadJobState.foregroundServiceId)
            val shadowNotificationContentPendingIntent = shadowOf(notification.contentIntent)
            assertTrue(shadowNotificationContentPendingIntent.isActivity)
        }

    private fun performSuccessfulCompleteDownload(
        testScope: CoroutineScope,
        testScheduler: TestCoroutineScheduler,
    ): DownloadJobState {
        val service = createService(browserStore, testScope, testScheduler)
        val download = DownloadState("https://example.com/file.txt", "file.txt")
        val response =
            Response(
                "https://example.com/file.txt",
                200,
                MutableHeaders(),
                Response.Body(mock()),
            )
        doReturn(response).`when`(client).fetch(Request("https://example.com/file.txt"))

        val downloadIntent = Intent("ACTION_DOWNLOAD")
        downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

        browserStore.dispatch(DownloadAction.AddDownloadAction(download))
        service.onStartCommand(downloadIntent, 0, 0)
        testDispatcher.scheduler.advanceUntilIdle()

        val providedDownload = argumentCaptor<DownloadJobState>()
        verify(service).performDownload(providedDownload.capture(), anyBoolean())

        val downloadJobState = service.downloadJobs[providedDownload.value.state.id]!!
        service.setDownloadJobStatus(downloadJobState, COMPLETED)
        assertEquals(COMPLETED, service.getDownloadJobStatus(downloadJobState))

        testDispatcher.scheduler.advanceTimeBy(delayTime)
        testDispatcher.scheduler.runCurrent()
        return downloadJobState
    }

    @Test
    fun `notification is shown when download status is FAILED`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download = DownloadState("https://example.com/file.txt", "file.txt")
            val response =
                Response(
                    "https://example.com/file.txt",
                    200,
                    MutableHeaders(),
                    Response.Body(mock()),
                )
            doReturn(response).`when`(client).fetch(Request("https://example.com/file.txt"))

            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))
            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            val providedDownload = argumentCaptor<DownloadJobState>()
            verify(service).performDownload(providedDownload.capture(), anyBoolean())

            val downloadJobState = service.downloadJobs[providedDownload.value.state.id]!!
            service.setDownloadJobStatus(downloadJobState, FAILED)
            assertEquals(FAILED, service.getDownloadJobStatus(downloadJobState))

            testDispatcher.scheduler.advanceTimeBy(delayTime)
            testDispatcher.scheduler.runCurrent()

            // one of the notifications it is the group notification only for devices the support it
            assertEquals(2, shadowNotificationService.size())
        }

    @Test
    fun `notification is not shown when download status is CANCELLED`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download = DownloadState("https://example.com/file.txt", "file.txt")
            val response =
                Response(
                    "https://example.com/file.txt",
                    200,
                    MutableHeaders(),
                    Response.Body(mock()),
                )
            doReturn(response).`when`(client).fetch(Request("https://example.com/file.txt"))

            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))
            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            val providedDownload = argumentCaptor<DownloadJobState>()
            verify(service).performDownload(providedDownload.capture(), anyBoolean())

            val downloadJobState = service.downloadJobs[providedDownload.value.state.id]!!
            service.setDownloadJobStatus(downloadJobState, CANCELLED)
            assertEquals(CANCELLED, service.getDownloadJobStatus(downloadJobState))

            testDispatcher.scheduler.advanceTimeBy(delayTime)
            testDispatcher.scheduler.runCurrent()

            // The additional notification is the summary one (the notification group).
            assertEquals(1, shadowNotificationService.size())
        }

    @Test
    fun `job status is set to failed when an Exception is thrown while performDownload`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            doThrow(IOException()).`when`(client).fetch(any())
            val download = DownloadState("https://example.com/file.txt", "file.txt")

            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))
            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            val providedDownload = argumentCaptor<DownloadJobState>()
            verify(service).performDownload(providedDownload.capture(), anyBoolean())

            val downloadJobState = service.downloadJobs[providedDownload.value.state.id]!!
            assertEquals(FAILED, service.getDownloadJobStatus(downloadJobState))
        }

    @Test
    fun `WHEN a download is from a private session the request must be private`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val response =
                Response(
                    "https://example.com/file.txt",
                    200,
                    MutableHeaders(),
                    Response.Body(mock()),
                )
            doReturn(response).`when`(client).fetch(any())
            val download = DownloadState("https://example.com/file.txt", "file.txt", private = true)
            val downloadJob = DownloadJobState(state = download, status = DOWNLOADING)
            val providedRequest = argumentCaptor<Request>()

            service.performDownload(downloadJob)
            verify(client).fetch(providedRequest.capture())
            assertTrue(providedRequest.value.private)

            downloadJob.state = download.copy(private = false)
            service.performDownload(downloadJob)

            verify(client, times(2)).fetch(providedRequest.capture())

            assertFalse(providedRequest.value.private)
        }

    @Test
    fun `performDownload - use the download response when available`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val responseFromDownloadState = mock<Response>()
            val responseFromClient = mock<Response>()
            val download =
                DownloadState(
                    "https://example.com/file.txt",
                    "file.txt",
                    response = responseFromDownloadState,
                    contentLength = 1000,
                )
            val downloadJob = DownloadJobState(state = download, status = DOWNLOADING)

            doReturn(404).`when`(responseFromDownloadState).status
            doReturn(responseFromClient).`when`(client).fetch(any())

            service.performDownload(downloadJob)

            verify(responseFromDownloadState, atLeastOnce()).status
            verifyNoInteractions(client)
        }

    @Test
    fun `performDownload - use the client response when the download response NOT available`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val responseFromClient = mock<Response>()
            val download =
                DownloadState("https://example.com/file.txt", "file.txt", response = null, contentLength = 1000)
            val downloadJob = DownloadJobState(state = download, status = DOWNLOADING)

            doReturn(404).`when`(responseFromClient).status
            doReturn(responseFromClient).`when`(client).fetch(any())

            service.performDownload(downloadJob)

            verify(responseFromClient, atLeastOnce()).status
        }

    @Test
    fun `performDownload - use the client response when resuming a download`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val responseFromDownloadState = mock<Response>()
            val responseFromClient = mock<Response>()
            val download =
                DownloadState(
                    "https://example.com/file.txt",
                    "file.txt",
                    response = responseFromDownloadState,
                    contentLength = 1000,
                )
            val downloadJob = DownloadJobState(currentBytesCopied = 100, state = download, status = DOWNLOADING)

            doReturn(404).`when`(responseFromClient).status
            doReturn(responseFromClient).`when`(client).fetch(any())

            service.performDownload(downloadJob)

            verify(responseFromClient, atLeastOnce()).status
            verifyNoInteractions(responseFromDownloadState)
        }

    @Test
    fun `performDownload - append when resuming a download with partial content response`() =
        runTest(testDispatcher) {
            val downloadFileWriter = FakeDownloadFileWriter(executeBlock = true)
            val service =
                createService(
                    browserStore,
                    backgroundScope,
                    testScheduler,
                    downloadFileWriter = downloadFileWriter,
                )
            val response =
                Response(
                    "https://example.com/file.txt",
                    206,
                    MutableHeaders("Content-Range" to "bytes 100-999/1000"),
                    Response.Body(mock()),
                )
            val download = DownloadState("https://example.com/file.txt", "file.txt", contentLength = 1000)
            val downloadJob = DownloadJobState(currentBytesCopied = 100, state = download, status = DOWNLOADING)

            doReturn(response).`when`(client).fetch(any())
            doReturn(ERROR_IN_STREAM_CLOSED).`when`(service).copyInChunks(any(), any(), any(), anyBoolean())

            service.performDownload(downloadJob)

            assertEquals(true, downloadFileWriter.lastAppend)
            assertEquals(100L, downloadJob.currentBytesCopied)
            assertEquals(DOWNLOADING, service.getDownloadJobStatus(downloadJob))
        }

    @Test
    fun `performDownload - resumed download with unknown size uses Content-Range total length`() =
        runTest(testDispatcher) {
            val service =
                createService(
                    browserStore,
                    backgroundScope,
                    testScheduler,
                    downloadFileWriter = FakeDownloadFileWriter(executeBlock = true),
                )
            val response =
                Response(
                    "https://example.com/file.txt",
                    206,
                    MutableHeaders(
                        "Content-Range" to "bytes 100-199/200",
                        "Content-Length" to "100",
                    ),
                    Response.Body(mock()),
                )
            val download = DownloadState("https://example.com/file.txt", "file.txt", contentLength = null)
            val downloadJob = DownloadJobState(currentBytesCopied = 100, state = download, status = DOWNLOADING)

            doReturn(response).`when`(client).fetch(any())
            doReturn(AbstractFetchDownloadService.CopyInChuckStatus.COMPLETED)
                .`when`(service)
                .copyInChunks(any(), any(), any(), anyBoolean())

            service.performDownload(downloadJob)

            assertEquals(200L, downloadJob.state.contentLength)
            assertEquals(FAILED, service.getDownloadJobStatus(downloadJob))
            verify(service, never()).setDownloadJobStatus(downloadJob, COMPLETED)
        }

    @Test
    fun `performDownload - restart from beginning when resume response ignores Range`() =
        runTest(testDispatcher) {
            var deleteMediaFileCalled = false
            val downloadFileWriter = FakeDownloadFileWriter(executeBlock = true)
            val staleResponse = mock<Response>()
            val service =
                createService(
                    browserStore,
                    backgroundScope,
                    testScheduler,
                    downloadFileUtils =
                        FakeDownloadFileUtils(
                            fileExists = { _, _ -> true },
                            deleteMediaFile = { _, _, _ ->
                                deleteMediaFileCalled = true
                                true
                            },
                        ),
                    downloadFileWriter = downloadFileWriter,
                )
            val response =
                Response(
                    "https://example.com/file.txt",
                    200,
                    MutableHeaders(),
                    Response.Body(mock()),
                )
            val download =
                DownloadState(
                    "https://example.com/file.txt",
                    "file.txt",
                    contentLength = 1000,
                    response = staleResponse,
                )
            val downloadJob = DownloadJobState(currentBytesCopied = 100, state = download, status = DOWNLOADING)

            doReturn(response).`when`(client).fetch(any())
            doReturn(ERROR_IN_STREAM_CLOSED).`when`(service).copyInChunks(any(), any(), any(), anyBoolean())

            service.performDownload(downloadJob)

            assertEquals(false, downloadFileWriter.lastAppend)
            assertEquals(0L, downloadJob.currentBytesCopied)
            assertEquals(0L, downloadJob.state.currentBytesCopied)
            assertEquals(null, downloadJob.state.response)
            assertTrue(deleteMediaFileCalled)
            assertEquals(DOWNLOADING, service.getDownloadJobStatus(downloadJob))
        }

    @Test
    fun `performDownload - restart from beginning when partial file is already missing`() =
        runTest(testDispatcher) {
            var deleteMediaFileCalled = false
            val downloadFileWriter = FakeDownloadFileWriter(executeBlock = true)
            val service =
                createService(
                    browserStore,
                    backgroundScope,
                    testScheduler,
                    downloadFileUtils =
                        FakeDownloadFileUtils(
                            fileExists = { _, _ -> false },
                            deleteMediaFile = { _, _, _ ->
                                deleteMediaFileCalled = true
                                true
                            },
                        ),
                    downloadFileWriter = downloadFileWriter,
                )
            val response =
                Response(
                    "https://example.com/file.txt",
                    200,
                    MutableHeaders(),
                    Response.Body(mock()),
                )
            val download = DownloadState("https://example.com/file.txt", "file.txt", contentLength = 1000)
            val downloadJob = DownloadJobState(currentBytesCopied = 100, state = download, status = DOWNLOADING)

            doReturn(response).`when`(client).fetch(any())
            doReturn(ERROR_IN_STREAM_CLOSED).`when`(service).copyInChunks(any(), any(), any(), anyBoolean())

            service.performDownload(downloadJob)

            assertEquals(false, downloadFileWriter.lastAppend)
            assertFalse(deleteMediaFileCalled)
            assertEquals(0L, downloadJob.currentBytesCopied)
            assertEquals(DOWNLOADING, service.getDownloadJobStatus(downloadJob))
        }

    @Test
    fun `performDownload - fail when partial file deletion fails before restart`() =
        runTest(testDispatcher) {
            var deleteMediaFileCalled = false
            val downloadFileWriter = FakeDownloadFileWriter(executeBlock = true)
            val staleResponse = mock<Response>()
            val service =
                createService(
                    browserStore,
                    backgroundScope,
                    testScheduler,
                    downloadFileUtils =
                        FakeDownloadFileUtils(
                            fileExists = { _, _ -> true },
                            deleteMediaFile = { _, _, _ ->
                                deleteMediaFileCalled = true
                                false
                            },
                        ),
                    downloadFileWriter = downloadFileWriter,
                )
            val response =
                Response(
                    "https://example.com/file.txt",
                    200,
                    MutableHeaders(),
                    Response.Body(mock()),
                )
            val download =
                DownloadState(
                    "https://example.com/file.txt",
                    "file.txt",
                    contentLength = 1000,
                    response = staleResponse,
                )
            val downloadJob = DownloadJobState(currentBytesCopied = 100, state = download, status = DOWNLOADING)

            doReturn(response).`when`(client).fetch(any())

            service.performDownload(downloadJob)

            assertTrue(deleteMediaFileCalled)
            assertEquals(null, downloadFileWriter.lastAppend)
            assertEquals(100L, downloadJob.currentBytesCopied)
            assertEquals(FAILED, service.getDownloadJobStatus(downloadJob))
        }

    @Test
    fun `performDownload - refresh content length when restart from beginning ignores stale resume metadata`() =
        runTest(testDispatcher) {
            val downloadFileWriter = FakeDownloadFileWriter()
            val service =
                createService(
                    browserStore,
                    backgroundScope,
                    testScheduler,
                    downloadFileUtils =
                        FakeDownloadFileUtils(
                            fileExists = { _, _ -> true },
                            deleteMediaFile = { _, _, _ -> true },
                        ),
                    downloadFileWriter = downloadFileWriter,
                )
            val response =
                Response(
                    "https://example.com/file.txt",
                    200,
                    MutableHeaders("Content-Length" to "2000"),
                    Response.Body(mock()),
                )
            val download = DownloadState("https://example.com/file.txt", "file.txt", contentLength = 1000)
            val downloadJob = DownloadJobState(currentBytesCopied = 100, state = download, status = DOWNLOADING)

            doReturn(response).`when`(client).fetch(any())

            service.performDownload(downloadJob)

            assertEquals(false, downloadFileWriter.lastAppend)
            assertEquals(2000L, downloadJob.state.contentLength)
        }

    @Test
    fun `performDownload - fail resumed download when partial content response lacks Content-Range`() =
        runTest(testDispatcher) {
            val downloadFileWriter = FakeDownloadFileWriter()
            val service =
                createService(
                    browserStore,
                    backgroundScope,
                    testScheduler,
                    downloadFileWriter = downloadFileWriter,
                )
            val response =
                Response(
                    "https://example.com/file.txt",
                    206,
                    MutableHeaders(),
                    Response.Body(mock()),
                )
            val download = DownloadState("https://example.com/file.txt", "file.txt", contentLength = 1000)
            val downloadJob = DownloadJobState(currentBytesCopied = 100, state = download, status = DOWNLOADING)

            doReturn(response).`when`(client).fetch(any())

            service.performDownload(downloadJob)

            assertEquals(null, downloadFileWriter.lastAppend)
            assertEquals(FAILED, service.getDownloadJobStatus(downloadJob))
        }

    @Test
    fun `performDownload - fail resumed download when Content-Range start does not match requested byte`() =
        runTest(testDispatcher) {
            val downloadFileWriter = FakeDownloadFileWriter()
            val service =
                createService(
                    browserStore,
                    backgroundScope,
                    testScheduler,
                    downloadFileWriter = downloadFileWriter,
                )
            val response =
                Response(
                    "https://example.com/file.txt",
                    206,
                    MutableHeaders("Content-Range" to "bytes 0-999/1000"),
                    Response.Body(mock()),
                )
            val download = DownloadState("https://example.com/file.txt", "file.txt", contentLength = 1000)
            val downloadJob = DownloadJobState(currentBytesCopied = 100, state = download, status = DOWNLOADING)

            doReturn(response).`when`(client).fetch(any())

            service.performDownload(downloadJob)

            assertEquals(null, downloadFileWriter.lastAppend)
            assertEquals(FAILED, service.getDownloadJobStatus(downloadJob))
        }

    @Test
    fun `performDownload - fail resumed download when response status is invalid`() =
        runTest(testDispatcher) {
            val downloadFileWriter = FakeDownloadFileWriter()
            val staleResponse = mock<Response>()
            val service =
                createService(
                    browserStore,
                    backgroundScope,
                    testScheduler,
                    downloadFileWriter = downloadFileWriter,
                )
            val response =
                Response(
                    "https://example.com/file.txt",
                    400,
                    MutableHeaders(),
                    Response.Body(mock()),
                )
            val download =
                DownloadState(
                    "https://example.com/file.txt",
                    "file.txt",
                    contentLength = 1000,
                    response = staleResponse,
                )
            val downloadJob = DownloadJobState(currentBytesCopied = 100, state = download, status = DOWNLOADING)

            doReturn(response).`when`(client).fetch(any())

            service.performDownload(downloadJob)

            assertEquals(null, downloadFileWriter.lastAppend)
            assertEquals(FAILED, service.getDownloadJobStatus(downloadJob))
        }

    @Test
    fun `performDownload - don't make a client request when download is completed`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val responseFromDownloadState = mock<Response>()
            val download =
                DownloadState(
                    "https://example.com/file.txt",
                    "file.txt",
                    response = responseFromDownloadState,
                    contentLength = 1000,
                )
            val downloadJob = DownloadJobState(currentBytesCopied = 1000, state = download, status = DOWNLOADING)

            service.performDownload(downloadJob)

            verify(service).verifyDownload(downloadJob)
        }

    @Test
    fun `updateDownloadState must update the download state in the store and in the downloadJobs`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download =
                DownloadState(
                    "https://example.com/file.txt",
                    "file1.txt",
                    status = DOWNLOADING,
                )
            val downloadJob = DownloadJobState(state = mock(), status = DOWNLOADING)

            service.downloadJobs[download.id] = downloadJob

            service.updateDownloadState(download)

            assertEquals(download, service.downloadJobs[download.id]!!.state)
            captureActionsMiddleware.assertFirstAction(DownloadAction.UpdateDownloadAction::class) { action ->
                assertEquals(download, action.download)
            }
        }

    @Test
    fun `onTaskRemoved cancels all notifications on the shadow notification manager`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download = DownloadState("https://example.com/file.txt", "file.txt")
            val response =
                Response(
                    "https://example.com/file.txt",
                    200,
                    MutableHeaders(),
                    Response.Body(mock()),
                )
            doReturn(response).`when`(client).fetch(Request("https://example.com/file.txt"))

            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            browserStore.dispatch(DownloadAction.AddDownloadAction(download))
            service.registerNotificationActionsReceiver()
            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            val providedDownload = argumentCaptor<DownloadJobState>()
            verify(service).performDownload(providedDownload.capture(), anyBoolean())

            service.setDownloadJobStatus(service.downloadJobs[download.id]!!, PAUSED)

            // Advance the clock so that the poller posts a notification.
            testDispatcher.scheduler.advanceTimeBy(delayTime)
            testDispatcher.scheduler.runCurrent()
            assertEquals(2, shadowNotificationService.size())

            // Now simulate onTaskRemoved.
            service.onTaskRemoved(null)

            verify(service).stopSelf()
        }

    @Test
    fun `clearAllDownloadsNotificationsAndJobs cancels all running jobs and remove all notifications`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download =
                DownloadState(
                    id = "1",
                    url = "https://example.com/file.txt",
                    fileName = "file.txt",
                    status = DOWNLOADING,
                )
            val downloadState =
                DownloadJobState(
                    state = download,
                    foregroundServiceId = Random.nextInt(),
                    status = DOWNLOADING,
                    job =
                        backgroundScope.launch {
                            @Suppress("ControlFlowWithEmptyBody") while (true) {}
                        },
                )

            service.registerNotificationActionsReceiver()
            service.downloadJobs[download.id] = downloadState

            val notificationStyle = AbstractFetchDownloadService.Style()
            val notification =
                DownloadNotification.createOngoingDownloadNotification(
                    context = testContext,
                    downloadState = downloadState.state,
                    fileSizeFormatter = fakeFileSizeFormatter,
                    notificationAccentColor = notificationStyle.notificationAccentColor,
                    downloadEstimator = fakeDownloadEstimator,
                )

            NotificationManagerCompat.from(testContext).notify(downloadState.foregroundServiceId, notification)

            // We have a pending notification
            assertEquals(1, shadowNotificationService.size())

            service.clearAllDownloadsNotificationsAndJobs()

            // Assert that all currently shown notifications are gone.
            assertEquals(0, shadowNotificationService.size())

            // Assert that jobs were cancelled rather than completed.
            service.downloadJobs.values.forEach {
                assertTrue(it.job!!.isCancelled)
                assertFalse(it.job!!.isCompleted)
            }
        }

    @Test
    fun `WHEN clearAllDownloadsNotificationsAndJobs is called THEN all non-completed downloads are cancelled`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val inProgressDownload =
                DownloadState(
                    id = "1",
                    url = "https://example.com/file.txt",
                    fileName = "file.txt",
                    status = DOWNLOADING,
                )
            val inProgressDownloadState =
                DownloadJobState(
                    state = inProgressDownload,
                    foregroundServiceId = Random.nextInt(),
                    status = DOWNLOADING,
                    job =
                        backgroundScope.launch {
                            @Suppress("ControlFlowWithEmptyBody") while (true) {}
                        },
                )

            val pausedDownload =
                DownloadState(
                    id = "2",
                    url = "https://example.com/file.txt",
                    fileName = "file.txt",
                    status = PAUSED,
                )
            val pausedDownloadState =
                DownloadJobState(
                    state = pausedDownload,
                    foregroundServiceId = Random.nextInt(),
                    status = PAUSED,
                    job =
                        backgroundScope.launch {
                            @Suppress("ControlFlowWithEmptyBody") while (true) {}
                        },
                )
            val initiatedDownload =
                DownloadState(
                    id = "3",
                    url = "https://example.com/file.txt",
                    fileName = "file.txt",
                    status = INITIATED,
                )
            val initiatedDownloadState =
                DownloadJobState(
                    state = initiatedDownload,
                    foregroundServiceId = Random.nextInt(),
                    status = INITIATED,
                    job =
                        backgroundScope.launch {
                            @Suppress("ControlFlowWithEmptyBody") while (true) {}
                        },
                )
            val failedDownload =
                DownloadState(
                    id = "4",
                    url = "https://example.com/file.txt",
                    fileName = "file.txt",
                    status = FAILED,
                )
            val failedDownloadState =
                DownloadJobState(
                    state = failedDownload,
                    foregroundServiceId = Random.nextInt(),
                    status = FAILED,
                    job =
                        backgroundScope.launch {
                            @Suppress("ControlFlowWithEmptyBody") while (true) {}
                        },
                )

            service.downloadJobs[inProgressDownload.id] = inProgressDownloadState
            service.downloadJobs[pausedDownload.id] = pausedDownloadState
            service.downloadJobs[initiatedDownload.id] = initiatedDownloadState
            service.downloadJobs[failedDownload.id] = failedDownloadState

            service.clearAllDownloadsNotificationsAndJobs()

            // Assert that jobs were cancelled rather than completed.
            service.downloadJobs.values.forEach {
                assertTrue(it.job!!.isCancelled)
                assertFalse(it.job!!.isCompleted)
                assertTrue(it.state.status == CANCELLED)
                assertTrue(it.status == CANCELLED)
                verify(service).updateDownloadState(it.state.copy(status = CANCELLED))
            }
        }

    @Test
    fun `WHEN clearAllDownloadsNotificationsAndJobs is called THEN all completed and cancelled downloads are unaffected`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val completedDownload =
                DownloadState(
                    id = "1",
                    url = "https://example.com/file.txt",
                    fileName = "file.txt",
                    status = COMPLETED,
                )
            val completedDownloadState =
                DownloadJobState(
                    state = completedDownload,
                    foregroundServiceId = Random.nextInt(),
                    status = COMPLETED,
                    job =
                        backgroundScope.launch {
                            @Suppress("ControlFlowWithEmptyBody") while (true) {}
                        },
                )

            val cancelledDownload =
                DownloadState(
                    id = "2",
                    url = "https://example.com/file.txt",
                    fileName = "file.txt",
                    status = CANCELLED,
                )
            val cancelledDownloadState =
                DownloadJobState(
                    state = cancelledDownload,
                    foregroundServiceId = Random.nextInt(),
                    status = CANCELLED,
                    job =
                        backgroundScope.launch {
                            @Suppress("ControlFlowWithEmptyBody") while (true) {}
                        },
                )

            service.downloadJobs[completedDownload.id] = completedDownloadState
            service.downloadJobs[cancelledDownload.id] = cancelledDownloadState

            val expected =
                mapOf(
                    Pair(completedDownload.id, completedDownloadState.copy()),
                    Pair(cancelledDownload.id, cancelledDownloadState.copy()),
                )

            service.clearAllDownloadsNotificationsAndJobs()

            assertEquals(expected, service.downloadJobs)
        }

    @Test
    fun `onDestroy will remove all download notifications, jobs and will call unregisterNotificationActionsReceiver`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            service.registerNotificationActionsReceiver()

            service.onDestroy()

            verify(service).clearAllDownloadsNotificationsAndJobs()
            verify(service).unregisterNotificationActionsReceiver()
        }

    @Test
    fun `onTimeout will call service stopSelf`() =
        runTest(testDispatcher) {
            val service = spy(AbstractFetchDownloadService::class.java)
            val startId = 1
            val fgsType = 0

            service.onTimeout(startId, fgsType)

            verify(service).stopSelf()
        }

    @Test
    fun `register and unregister notification actions receiver`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            service.onCreate()

            verify(service).registerNotificationActionsReceiver()

            service.onDestroy()

            verify(service).unregisterNotificationActionsReceiver()
        }

    @Test
    @Config(sdk = [28])
    fun `WHEN a download is completed and the scoped storage is not used it MUST be added manually to the download system database`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download =
                DownloadState(
                    url = "http://www.mozilla.org",
                    fileName = "example.apk",
                    directoryPath = folder.root.path,
                    status = COMPLETED,
                )

            val downloadJobState = DownloadJobState(state = download, status = COMPLETED)

            service.updateDownloadNotification(COMPLETED, downloadJobState, backgroundScope)
            testDispatcher.scheduler.runCurrent()
            testDispatcher.scheduler.advanceUntilIdle()

            verify(service)
                .addCompletedDownload(
                    title = any(),
                    description = any(),
                    isMediaScannerScannable = eq(true),
                    mimeType = any(),
                    path = any(),
                    length = anyLong(),
                    showNotification = anyBoolean(),
                    download = any(),
                )
        }

    @Test
    fun `WHEN a download is completed and the scoped storage is NOT not used it MUST NOT be added manually to the download system database`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download =
                DownloadState(
                    url = "http://www.mozilla.org",
                    fileName = "example.apk",
                    directoryPath = folder.root.path,
                    status = COMPLETED,
                )

            val downloadJobState = DownloadJobState(state = download, status = COMPLETED)

            service.updateDownloadNotification(COMPLETED, downloadJobState, backgroundScope)

            verify(service, never())
                .addCompletedDownload(
                    title = any(),
                    description = any(),
                    isMediaScannerScannable = anyBoolean(),
                    mimeType = any(),
                    path = any(),
                    length = anyLong(),
                    showNotification = anyBoolean(),
                    download = any(),
                )
        }

    @Test
    fun `WHEN a download is completed and the scoped storage is used addToDownloadSystemDatabaseCompat MUST NOT be called`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download =
                DownloadState(
                    url = "http://www.mozilla.org",
                    fileName = "example.apk",
                    directoryPath = folder.root.path,
                    status = COMPLETED,
                )

            val downloadJobState = DownloadJobState(state = download, status = COMPLETED)

            doNothing()
                .`when`(service)
                .addCompletedDownload(
                    title = any(),
                    description = any(),
                    isMediaScannerScannable = eq(true),
                    mimeType = any(),
                    path = any(),
                    length = anyLong(),
                    showNotification = anyBoolean(),
                    download = any(),
                )
            doReturn(true).`when`(service).shouldUseScopedStorage()

            service.updateDownloadNotification(COMPLETED, downloadJobState, backgroundScope)

            verify(service, never())
                .addCompletedDownload(
                    title = any(),
                    description = any(),
                    isMediaScannerScannable = eq(true),
                    mimeType = any(),
                    path = any(),
                    length = anyLong(),
                    showNotification = anyBoolean(),
                    download = any(),
                )
        }

    @Test
    @Suppress("Deprecation")
    @Config(sdk = [28])
    fun `WHEN scoped storage is used do not pass non-http(s) url to addCompletedDownload`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download =
                DownloadState(
                    url = "blob:moz-extension://d5ea9baa-64c9-4c3d-bb38-49308c47997c/",
                    fileName = "example.apk",
                    directoryPath = folder.root.path,
                )

            val spyContext = spy(testContext)
            val downloadManager: DownloadManager = mock()

            doReturn(spyContext).`when`(service).context
            doReturn(downloadManager).`when`(spyContext).getSystemService<DownloadManager>()

            service.addToDownloadSystemDatabaseCompat(download, backgroundScope)
            testDispatcher.scheduler.runCurrent()
            testDispatcher.scheduler.advanceUntilIdle()

            verify(downloadManager)
                .addCompletedDownload(
                    anyString(),
                    anyString(),
                    anyBoolean(),
                    anyString(),
                    anyString(),
                    anyLong(),
                    anyBoolean(),
                    isNull(),
                    any(),
                )
        }

    @Test
    @Suppress("Deprecation")
    fun `GIVEN a download that throws an exception WHEN adding to the system database THEN handle the exception`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download =
                DownloadState(
                    url = "url",
                    fileName = "example.apk",
                    directoryPath = folder.root.path,
                )

            val spyContext = spy(testContext)
            val downloadManager: DownloadManager = mock()

            doReturn(spyContext).`when`(service).context
            doReturn(downloadManager).`when`(spyContext).getSystemService<DownloadManager>()

            doAnswer { throw IllegalArgumentException() }
                .`when`(downloadManager)
                .addCompletedDownload(
                    anyString(),
                    anyString(),
                    anyBoolean(),
                    anyString(),
                    anyString(),
                    anyLong(),
                    anyBoolean(),
                    isNull(),
                    any(),
                )

            try {
                service.addToDownloadSystemDatabaseCompat(download, backgroundScope)
            } catch (_: IOException) {
                fail()
            }
        }

    @Test
    @Suppress("Deprecation")
    @Config(sdk = [28])
    fun `WHEN scoped storage is used pass http(s) url to addCompletedDownload`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val download =
                DownloadState(
                    url = "https://mozilla.com",
                    fileName = "example.apk",
                    directoryPath = folder.root.path,
                )

            val spyContext = spy(testContext)
            val downloadManager: DownloadManager = mock()

            doReturn(spyContext).`when`(service).context
            doReturn(downloadManager).`when`(spyContext).getSystemService<DownloadManager>()

            service.addToDownloadSystemDatabaseCompat(download, backgroundScope)
            testDispatcher.scheduler.runCurrent()
            testDispatcher.scheduler.advanceUntilIdle()

            verify(downloadManager)
                .addCompletedDownload(
                    eq("example.apk"),
                    eq("example.apk"),
                    eq(true),
                    argThat { it != null && it.isNotEmpty() },
                    anyString(),
                    eq(0L),
                    eq(false),
                    eq("https://mozilla.com".toUri()),
                    eq(null),
                )
        }

    @Test
    @Suppress("Deprecation")
    @Config(sdk = [28])
    fun `WHEN scoped storage is used ALWAYS call addCompletedDownload with a not empty or null mimeType`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val spyContext = spy(testContext)
            val downloadManager: DownloadManager = mock()
            doReturn(spyContext).`when`(service).context
            doReturn(downloadManager).`when`(spyContext).getSystemService<DownloadManager>()
            val downloadWithNullMimeType =
                DownloadState(
                    url = "blob:moz-extension://d5ea9baa-64c9-4c3d-bb38-49308c47997c/",
                    fileName = "example.apk",
                    directoryPath = folder.root.path,
                    contentType = null,
                )

            service.addToDownloadSystemDatabaseCompat(downloadWithNullMimeType, backgroundScope)
            testDispatcher.scheduler.runCurrent()
            testDispatcher.scheduler.advanceUntilIdle()

            verify(downloadManager)
                .addCompletedDownload(
                    anyString(),
                    anyString(),
                    anyBoolean(),
                    argThat { it != null && it.isNotEmpty() },
                    anyString(),
                    anyLong(),
                    anyBoolean(),
                    isNull(),
                    any(),
                )

            clearInvocations(downloadManager)
            val downloadWithEmptyMimeType = downloadWithNullMimeType.copy(contentType = "")

            service.addToDownloadSystemDatabaseCompat(downloadWithEmptyMimeType, backgroundScope)
            testDispatcher.scheduler.runCurrent()
            testDispatcher.scheduler.advanceUntilIdle()

            verify(downloadManager)
                .addCompletedDownload(
                    anyString(),
                    anyString(),
                    anyBoolean(),
                    argThat { it != null && it.isNotEmpty() },
                    anyString(),
                    anyLong(),
                    anyBoolean(),
                    isNull(),
                    any(),
                )
        }

    @Test
    @Suppress("Deprecation")
    fun `WHEN scoped storage is NOT used NEVER call addCompletedDownload with a not empty or null mimeType`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val spyContext = spy(testContext)
            var downloadManager: DownloadManager = mock()
            doReturn(spyContext).`when`(service).context
            doReturn(downloadManager).`when`(spyContext).getSystemService<DownloadManager>()
            val downloadWithNullMimeType =
                DownloadState(
                    url = "blob:moz-extension://d5ea9baa-64c9-4c3d-bb38-49308c47997c/",
                    fileName = "example.apk",
                    directoryPath = folder.root.path,
                    contentType = null,
                )
            val downloadWithEmptyMimeType = downloadWithNullMimeType.copy(contentType = "")
            val defaultMimeType = "*/*"

            service.addToDownloadSystemDatabaseCompat(downloadWithNullMimeType, backgroundScope)
            verify(downloadManager, never())
                .addCompletedDownload(
                    anyString(),
                    anyString(),
                    anyBoolean(),
                    eq(defaultMimeType),
                    anyString(),
                    anyLong(),
                    anyBoolean(),
                    isNull(),
                    any(),
                )

            downloadManager = mock()
            doReturn(downloadManager).`when`(spyContext).getSystemService<DownloadManager>()
            service.addToDownloadSystemDatabaseCompat(downloadWithEmptyMimeType, backgroundScope)
            verify(downloadManager, never())
                .addCompletedDownload(
                    anyString(),
                    anyString(),
                    anyBoolean(),
                    eq(defaultMimeType),
                    anyString(),
                    anyLong(),
                    anyBoolean(),
                    isNull(),
                    any(),
                )
        }

    @Test
    fun `cancelled download does not prevent other notifications`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val cancelledDownload = DownloadState("https://example.com/file.txt", "file.txt")
            val response =
                Response(
                    "https://example.com/file.txt",
                    200,
                    MutableHeaders(),
                    Response.Body(mock()),
                )

            doReturn(response).`when`(client).fetch(Request("https://example.com/file.txt"))
            val cancelledDownloadIntent = Intent("ACTION_DOWNLOAD")
            cancelledDownloadIntent.putExtra(EXTRA_DOWNLOAD_ID, cancelledDownload.id)

            browserStore.dispatch(DownloadAction.AddDownloadAction(cancelledDownload))
            service.onStartCommand(cancelledDownloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            val providedDownload = argumentCaptor<DownloadJobState>()

            verify(service).performDownload(providedDownload.capture(), anyBoolean())

            val cancelledDownloadJobState = service.downloadJobs[providedDownload.value.state.id]!!

            service.setDownloadJobStatus(cancelledDownloadJobState, CANCELLED)
            assertEquals(CANCELLED, service.getDownloadJobStatus(cancelledDownloadJobState))
            testDispatcher.scheduler.advanceTimeBy(delayTime)
            testDispatcher.scheduler.runCurrent()
            // The additional notification is the summary one (the notification group).
            assertEquals(1, shadowNotificationService.size())

            val download = DownloadState("https://example.com/file.txt", "file.txt")
            val downloadIntent = Intent("ACTION_DOWNLOAD")
            downloadIntent.putExtra(EXTRA_DOWNLOAD_ID, download.id)

            // Start another download to ensure its notifications are presented
            browserStore.dispatch(DownloadAction.AddDownloadAction(download))
            service.onStartCommand(downloadIntent, 0, 0)
            testDispatcher.scheduler.advanceUntilIdle()

            verify(service, times(2)).performDownload(providedDownload.capture(), anyBoolean())

            val downloadJobState = service.downloadJobs[providedDownload.value.state.id]!!

            service.setDownloadJobStatus(downloadJobState, COMPLETED)
            assertEquals(COMPLETED, service.getDownloadJobStatus(downloadJobState))
            testDispatcher.scheduler.advanceTimeBy(delayTime)
            testDispatcher.scheduler.runCurrent()
            // one of the notifications it is the group notification only for devices the support it
            assertEquals(2, shadowNotificationService.size())
        }

    @Test
    fun `keeps track of how many seconds have passed since the last update to a notification`() =
        runTest(testDispatcher) {
            val fakeClock = FakeDateTimeProvider(testScheduler)

            val downloadJobState = DownloadJobState(state = mock(), status = DOWNLOADING, dateTimeProvider = fakeClock)
            val oneSecond = 1000L

            downloadJobState.lastNotificationUpdate = fakeClock.currentTimeMillis()

            delay(oneSecond)

            var seconds = downloadJobState.getSecondsSinceTheLastNotificationUpdate()

            assertEquals(1, seconds)

            delay(oneSecond)

            seconds = downloadJobState.getSecondsSinceTheLastNotificationUpdate()

            assertEquals(2, seconds)
        }

    @OptIn(ExperimentalCoroutinesApi::class) // currentTime
    @Test
    fun `is a notification under the time limit for updates`() =
        runTest(testDispatcher) {
            val fakeClock = FakeDateTimeProvider(testScheduler)

            val downloadJobState =
                DownloadJobState(
                    state = mock(),
                    status = DOWNLOADING,
                    dateTimeProvider = fakeClock,
                )
            val oneSecond = 1000L

            downloadJobState.lastNotificationUpdate = fakeClock.currentTimeMillis()

            assertFalse(downloadJobState.isUnderNotificationUpdateLimit())

            delay(oneSecond)

            assertTrue(downloadJobState.isUnderNotificationUpdateLimit())
        }

    @Test
    fun `try to update a notification`() =
        runTest(testDispatcher) {
            val fakeClock = FakeDateTimeProvider(testScheduler)

            val downloadJobState = DownloadJobState(state = mock(), status = DOWNLOADING, dateTimeProvider = fakeClock)
            val oneSecond = 1000L

            downloadJobState.lastNotificationUpdate = fakeClock.currentTimeMillis()

            // It's over the notification limit
            assertFalse(downloadJobState.canUpdateNotification())

            delay(oneSecond)

            // It's under the notification limit
            assertTrue(downloadJobState.canUpdateNotification())

            downloadJobState.notifiedStopped = true

            assertFalse(downloadJobState.canUpdateNotification())

            downloadJobState.notifiedStopped = false

            assertTrue(downloadJobState.canUpdateNotification())
        }

    @Test
    fun `copyInChunks must alter download currentBytesCopied`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val downloadJobState = DownloadJobState(state = mock(), status = DOWNLOADING)
            val inputStream = mock<InputStream>()

            assertEquals(0, downloadJobState.currentBytesCopied)

            doReturn(15, -1).`when`(inputStream).read(any())
            doNothing().`when`(service).updateDownloadState(any())

            service.copyInChunks(downloadJobState, inputStream, mock())

            assertEquals(15, downloadJobState.currentBytesCopied)
        }

    @Test
    fun `copyInChunks - must return ERROR_IN_STREAM_CLOSED when inStream is closed`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val downloadJobState = DownloadJobState(state = mock(), status = DOWNLOADING)
            val inputStream = mock<InputStream>()

            assertEquals(0, downloadJobState.currentBytesCopied)

            doAnswer { throw IOException() }.`when`(inputStream).read(any())
            doNothing().`when`(service).updateDownloadState(any())
            doNothing().`when`(service).performDownload(any(), anyBoolean())

            val status = service.copyInChunks(downloadJobState, inputStream, mock())

            verify(service).performDownload(downloadJobState, true)
            assertEquals(ERROR_IN_STREAM_CLOSED, status)
        }

    @Test
    fun `copyInChunks - must throw when inStream is closed and download was performed using http client`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val downloadJobState = DownloadJobState(state = mock(), status = DOWNLOADING)
            val inputStream = mock<InputStream>()
            var exceptionWasThrown = false

            assertEquals(0, downloadJobState.currentBytesCopied)

            doAnswer { throw IOException() }.`when`(inputStream).read(any())
            doNothing().`when`(service).updateDownloadState(any())
            doNothing().`when`(service).performDownload(any(), anyBoolean())

            try {
                service.copyInChunks(downloadJobState, inputStream, mock(), true)
            } catch (_: IOException) {
                exceptionWasThrown = true
            }

            verify(service, times(0)).performDownload(downloadJobState, true)
            assertTrue(exceptionWasThrown)
        }

    @Test
    fun `copyInChunks - must return COMPLETED when finish copying bytes`() =
        runTest(testDispatcher) {
            val service = createService(browserStore, backgroundScope, testScheduler)
            val downloadJobState = DownloadJobState(state = mock(), status = DOWNLOADING)
            val inputStream = mock<InputStream>()

            assertEquals(0, downloadJobState.currentBytesCopied)

            doReturn(15, -1).`when`(inputStream).read(any())
            doNothing().`when`(service).updateDownloadState(any())

            val status = service.copyInChunks(downloadJobState, inputStream, mock())

            verify(service, never()).performDownload(any(), anyBoolean())

            assertEquals(15, downloadJobState.currentBytesCopied)
            assertEquals(AbstractFetchDownloadService.CopyInChuckStatus.COMPLETED, status)
        }

    @Test
    fun `WHEN cancelDownloadJob is called THEN deleteDownloadingFile must be called`() =
        runTest(testDispatcher) {
            var deleteWasCalled = false
            val fakeDownloadFileUtils =
                FakeDownloadFileUtils(
                    deleteMediaFile = { _, _, _ ->
                        deleteWasCalled = true
                        true
                    }
                )
            val service =
                createService(
                    browserStore = browserStore,
                    testScope = backgroundScope,
                    scheduler = testScheduler,
                    downloadFileUtils = fakeDownloadFileUtils,
                )
            val downloadState = DownloadState(url = "mozilla.org/mozilla.txt")
            val downloadJobState = DownloadJobState(job = Job(), state = downloadState, status = DOWNLOADING)

            service.downloadJobs[downloadState.id] = downloadJobState

            service.cancelDownloadJob(
                currentDownloadJobState = downloadJobState,
                coroutineScope = TestScope(testDispatcher),
            )
            testDispatcher.scheduler.advanceUntilIdle()

            assertTrue(deleteWasCalled)
            assertTrue(downloadJobState.downloadDeleted)
        }
}

@Implements(FileProvider::class)
object ShadowFileProvider {
    @Implementation
    @JvmStatic
    @Suppress("UNUSED_PARAMETER")
    fun getUriForFile(
        context: Context?,
        authority: String?,
        file: File,
    ) = "content://authority/random/location/${file.name}".toUri()
}

@OptIn(ExperimentalCoroutinesApi::class)
class FakeDateTimeProvider(private val scheduler: TestCoroutineScheduler) : DateTimeProvider {
    override fun currentTimeMillis(): Long = scheduler.currentTime

    override fun currentLocalDate(): LocalDate = LocalDate.ofEpochDay(scheduler.currentTime / 86_400_000)

    override fun currentZoneId(): ZoneId = ZoneId.of("UTC")
}
