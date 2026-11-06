/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.downloads

import android.Manifest.permission.INTERNET
import android.Manifest.permission.WRITE_EXTERNAL_STORAGE
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.pm.PackageManager
import android.content.pm.ResolveInfo
import androidx.fragment.app.FragmentManager
import androidx.fragment.app.FragmentTransaction
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertNotNull
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.content.DownloadState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.downloads.DownloadsUseCases.CancelDownloadRequestUseCase
import mozilla.components.feature.downloads.DownloadsUseCases.ConsumeDownloadUseCase
import mozilla.components.feature.downloads.ext.getRealFilenameOrGuessed
import mozilla.components.feature.downloads.fake.FakeFileSystemHelper
import mozilla.components.feature.downloads.manager.DownloadManager
import mozilla.components.feature.downloads.ui.DownloadAppChooserDialog
import mozilla.components.feature.downloads.ui.DownloaderApp
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.eq
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.grantPermission
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import mozilla.components.support.utils.FakeDownloadFileUtils
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.ArgumentMatchers
import org.mockito.ArgumentMatchers.anyInt
import org.mockito.ArgumentMatchers.anyString
import org.mockito.Mockito.doNothing
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.doThrow
import org.mockito.Mockito.never
import org.mockito.Mockito.spy
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.robolectric.annotation.Config
import org.robolectric.shadows.ShadowToast

@RunWith(AndroidJUnit4::class)
class DownloadsFeatureTest {
    private val testDispatcher = StandardTestDispatcher()

    private lateinit var store: BrowserStore

    @Before
    fun setUp() {
        store =
            BrowserStore(
                BrowserState(
                    tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")),
                    selectedTabId = "test-tab",
                )
            )
    }

    @Test
    fun `Adding a download object will request permissions if needed`() =
        runTest(testDispatcher) {
            val fragmentManager: FragmentManager = mock()

            val download = DownloadState(url = "https://www.mozilla.org", sessionId = "test-tab")

            var requestedPermissions = false

            val feature =
                DownloadsFeature(
                    testContext,
                    store,
                    useCases = mock(),
                    onNeedToRequestPermissions = { requestedPermissions = true },
                    fragmentManager = mockFragmentManager(),
                    mainDispatcher = testDispatcher,
                    downloadFileUtils = FakeDownloadFileUtils(),
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            assertFalse(requestedPermissions)

            store.dispatch(ContentAction.UpdateDownloadAction("test-tab", download))
            testDispatcher.scheduler.advanceUntilIdle()

            assertTrue(requestedPermissions)
            verify(fragmentManager, never()).beginTransaction()
        }

    @Test
    fun `Adding a download when permissions are granted will show dialog`() =
        runTest(testDispatcher) {
            val fragmentManager: FragmentManager = mockFragmentManager()

            grantPermissions()

            val feature =
                DownloadsFeature(
                    testContext,
                    store,
                    useCases = mock(),
                    fragmentManager = fragmentManager,
                    mainDispatcher = testDispatcher,
                    downloadFileUtils = FakeDownloadFileUtils(),
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            verify(fragmentManager, never()).beginTransaction()
            val download = DownloadState(url = "https://www.mozilla.org", sessionId = "test-tab")

            store.dispatch(ContentAction.UpdateDownloadAction("test-tab", download))

            testDispatcher.scheduler.advanceUntilIdle()

            verify(fragmentManager).beginTransaction()
        }

    @Test
    fun `Try again calls download manager`() =
        runTest(testDispatcher) {
            val fragmentManager: FragmentManager = mockFragmentManager()

            val downloadManager: DownloadManager = mock()

            grantPermissions()

            val feature =
                DownloadsFeature(
                    testContext,
                    store,
                    useCases = mock(),
                    fragmentManager = fragmentManager,
                    downloadFileUtils = FakeDownloadFileUtils(),
                    downloadManager = downloadManager,
                    mainDispatcher = testDispatcher,
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            feature.tryAgain("0")

            verify(downloadManager).tryAgain("0")
        }

    @Test
    fun `Adding a download without a fragment manager will start download immediately`() =
        runTest(testDispatcher) {
            grantPermissions()

            val downloadManager: DownloadManager = mock()
            doReturn(arrayOf(INTERNET, WRITE_EXTERNAL_STORAGE)).`when`(downloadManager).permissions

            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        useCases = DownloadsUseCases(store, mock()),
                        downloadFileUtils = FakeDownloadFileUtils(),
                        downloadManager = downloadManager,
                        mainDispatcher = testDispatcher,
                    )
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            verify(downloadManager, never()).download(any(), anyString())

            val download =
                DownloadState(
                    url = "https://www.mozilla.org",
                    sessionId = "test-tab",
                    directoryPath = "/storage/emulated/0/Download",
                )
            doReturn("id").`when`(downloadManager).download(download)
            doReturn(false).`when`(feature).isDownloadBiggerThanAvailableSpace(download)

            store.dispatch(ContentAction.UpdateDownloadAction("test-tab", download))
            testDispatcher.scheduler.advanceUntilIdle()

            verify(downloadManager).download(eq(download), anyString())
        }

    @Test
    fun `Adding a Download with skipConfirmation flag will start download immediately`() =
        runTest(testDispatcher) {
            val fragmentManager: FragmentManager = mockFragmentManager()

            grantPermissions()

            val downloadManager: DownloadManager = mock()
            doReturn(arrayOf(INTERNET, WRITE_EXTERNAL_STORAGE)).`when`(downloadManager).permissions

            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        useCases = DownloadsUseCases(store, mock()),
                        downloadFileUtils = FakeDownloadFileUtils(),
                        downloadManager = downloadManager,
                        fragmentManager = fragmentManager,
                        mainDispatcher = testDispatcher,
                    )
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            verify(fragmentManager, never()).beginTransaction()

            val download =
                DownloadState(
                    url = "https://www.mozilla.org",
                    skipConfirmation = true,
                    sessionId = "test-tab",
                    directoryPath = "/storage/emulated/0/Download",
                )

            doReturn("id").`when`(downloadManager).download(eq(download), anyString())
            doReturn(false).`when`(feature).isDownloadBiggerThanAvailableSpace(download)

            store.dispatch(ContentAction.UpdateDownloadAction("test-tab", download))
            testDispatcher.scheduler.advanceUntilIdle()

            verify(fragmentManager, never()).beginTransaction()
            verify(downloadManager).download(eq(download), anyString())

            assertNull(store.state.findTab("test-tab")!!.content.download)
        }

    @Test
    fun `When starting a download an existing dialog is reused`() =
        runTest(testDispatcher) {
            grantPermissions()

            val download = DownloadState(url = "https://www.mozilla.org", sessionId = "test-tab", fileName = "fileName")
            store.dispatch(ContentAction.UpdateDownloadAction("test-tab", download))

            val dialogFragment: DownloadDialogFragment = mock()
            val fragmentManager: FragmentManager = mock()
            doReturn(dialogFragment).`when`(fragmentManager).findFragmentByTag(DownloadDialogFragment.FRAGMENT_TAG)

            val downloadManager: DownloadManager = mock()
            doReturn(arrayOf(INTERNET, WRITE_EXTERNAL_STORAGE)).`when`(downloadManager).permissions

            val feature =
                DownloadsFeature(
                    testContext,
                    store,
                    useCases = mock(),
                    downloadManager = downloadManager,
                    downloadFileUtils = FakeDownloadFileUtils(),
                    fragmentManager = fragmentManager,
                    mainDispatcher = testDispatcher,
                )

            val tab = store.state.findTab("test-tab")
            feature.showDownloadDialog(
                tab = tab!!,
                download = download,
            )

            verify(dialogFragment).onStartDownload = any()
            verify(dialogFragment).onCancelDownload = any()
            verify(dialogFragment).setDownload(any(), eq(download.getRealFilenameOrGuessed(FakeDownloadFileUtils())))
            verify(dialogFragment, never()).showNow(any(), any())
        }

    @Test
    fun `WHEN dismissing a download dialog THEN the download stream should be closed`() =
        runTest(testDispatcher) {
            val downloadsUseCases = spy(DownloadsUseCases(store, mock()))
            val closeDownloadResponseUseCase = mock<CancelDownloadRequestUseCase>()
            val download = DownloadState(url = "https://www.mozilla.org", sessionId = "test-tab")
            val dialogFragment = spy(object : DownloadDialogFragment() {})
            val fragmentManager: FragmentManager = mock()

            doReturn(dialogFragment).`when`(fragmentManager).findFragmentByTag(DownloadDialogFragment.FRAGMENT_TAG)
            store.dispatch(ContentAction.UpdateDownloadAction("test-tab", download))
            doReturn(closeDownloadResponseUseCase).`when`(downloadsUseCases).cancelDownloadRequest

            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        useCases = downloadsUseCases,
                        downloadManager = mock(),
                        fragmentManager = fragmentManager,
                        mainDispatcher = testDispatcher,
                        downloadFileUtils = FakeDownloadFileUtils(),
                    )
                )

            val tab = store.state.findTab("test-tab")

            feature.showDownloadDialog(
                tab = tab!!,
                download = download,
            )

            dialogFragment.onCancelDownload()
            verify(closeDownloadResponseUseCase).invoke(anyString(), anyString())
        }

    @Test
    fun `onPermissionsResult will start download if permissions were granted and thirdParty enabled`() =
        runTest(testDispatcher) {
            val downloadsUseCases = spy(DownloadsUseCases(store, mock()))
            val consumeDownloadUseCase = mock<ConsumeDownloadUseCase>()
            val download = DownloadState(url = "https://www.mozilla.org", sessionId = "test-tab")
            val downloadManager: DownloadManager = mock()
            val permissionsArray = arrayOf(INTERNET, WRITE_EXTERNAL_STORAGE)
            val grantedPermissionsArray =
                arrayOf(PackageManager.PERMISSION_GRANTED, PackageManager.PERMISSION_GRANTED).toIntArray()

            store.dispatch(ContentAction.UpdateDownloadAction("test-tab", download = download))

            doReturn(permissionsArray).`when`(downloadManager).permissions
            doReturn(consumeDownloadUseCase).`when`(downloadsUseCases).consumeDownload

            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        useCases = downloadsUseCases,
                        downloadManager = downloadManager,
                        shouldForwardToThirdParties = { true },
                        mainDispatcher = testDispatcher,
                        downloadFileUtils = FakeDownloadFileUtils(),
                    )
                )

            doReturn(false).`when`(feature).startDownload(any())

            grantPermissions()

            feature.onPermissionsResult(permissionsArray, grantedPermissionsArray)

            verify(feature).startDownload(download)
            verify(feature, never()).processDownload(any(), eq(download))
            verify(consumeDownloadUseCase).invoke(anyString(), anyString())
        }

    @Test
    fun `onPermissionsResult will process download if permissions were granted and thirdParty disabled`() =
        runTest(testDispatcher) {
            val downloadsUseCases = spy(DownloadsUseCases(store, mock()))
            val consumeDownloadUseCase = mock<ConsumeDownloadUseCase>()
            val download =
                DownloadState(
                    url = "https://www.mozilla.org",
                    sessionId = "test-tab",
                    directoryPath = "/storage/emulated/0/Download",
                )
            val downloadManager: DownloadManager = mock()
            val permissionsArray = arrayOf(INTERNET, WRITE_EXTERNAL_STORAGE)
            val grantedPermissionsArray =
                arrayOf(PackageManager.PERMISSION_GRANTED, PackageManager.PERMISSION_GRANTED).toIntArray()

            store.dispatch(ContentAction.UpdateDownloadAction("test-tab", download = download))

            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        useCases = downloadsUseCases,
                        downloadManager = downloadManager,
                        shouldForwardToThirdParties = { false },
                        mainDispatcher = testDispatcher,
                        downloadFileUtils = FakeDownloadFileUtils(),
                    )
                )

            doReturn(permissionsArray).`when`(downloadManager).permissions
            doReturn(false).`when`(feature).processDownload(any(), any())

            grantPermissions()

            feature.onPermissionsResult(permissionsArray, grantedPermissionsArray)

            verify(feature).processDownload(any(), eq(download))
            verify(feature, never()).startDownload(download)
            verify(consumeDownloadUseCase, never()).invoke(anyString(), anyString())
        }

    @Test
    fun `onPermissionsResult will cancel the download if permissions were not granted`() =
        runTest(testDispatcher) {
            val closeDownloadResponseUseCase = mock<CancelDownloadRequestUseCase>()
            val store =
                BrowserStore(
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")),
                        selectedTabId = "test-tab",
                    )
                )
            val downloadsUseCases = spy(DownloadsUseCases(store, mock()))

            doReturn(closeDownloadResponseUseCase).`when`(downloadsUseCases).cancelDownloadRequest

            store.dispatch(
                ContentAction.UpdateDownloadAction(
                    "test-tab",
                    DownloadState("https://www.mozilla.org"),
                )
            )

            val downloadManager: DownloadManager = mock()
            doReturn(arrayOf(INTERNET, WRITE_EXTERNAL_STORAGE)).`when`(downloadManager).permissions

            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        useCases = downloadsUseCases,
                        downloadManager = downloadManager,
                        mainDispatcher = testDispatcher,
                        downloadFileUtils = FakeDownloadFileUtils(),
                    )
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            feature.onPermissionsResult(
                arrayOf(INTERNET, WRITE_EXTERNAL_STORAGE),
                arrayOf(PackageManager.PERMISSION_GRANTED, PackageManager.PERMISSION_DENIED).toIntArray(),
            )

            verify(downloadManager, never()).download(any(), anyString())
            verify(closeDownloadResponseUseCase).invoke(anyString(), anyString())
            verify(feature).showPermissionDeniedDialog()
        }

    @Test
    fun `Calling stop() will unregister listeners from download manager`() =
        runTest(testDispatcher) {
            val downloadManager: DownloadManager = mock()

            val feature =
                DownloadsFeature(
                    testContext,
                    store,
                    useCases = mock(),
                    downloadManager = downloadManager,
                    downloadFileUtils = FakeDownloadFileUtils(),
                    mainDispatcher = testDispatcher,
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            verify(downloadManager, never()).unregisterListeners()

            feature.stop()

            verify(downloadManager).unregisterListeners()
        }

    @Test
    fun `Calling start() will register listeners from download manager`() =
        runTest(testDispatcher) {
            val downloadManager: DownloadManager = mock()

            val feature =
                DownloadsFeature(
                    testContext,
                    store,
                    useCases = mock(),
                    downloadManager = downloadManager,
                    downloadFileUtils = FakeDownloadFileUtils(),
                    mainDispatcher = testDispatcher,
                )
            verify(downloadManager, never()).registerListeners()

            feature.start()

            testDispatcher.scheduler.advanceUntilIdle()

            verify(downloadManager).registerListeners()
        }

    @Test
    fun `DownloadManager failing to start download will cause error toast to be displayed`() =
        runTest(testDispatcher) {
            grantPermissions()

            val downloadManager: DownloadManager = mock()
            doReturn(arrayOf(INTERNET, WRITE_EXTERNAL_STORAGE)).`when`(downloadManager).permissions

            doReturn(null).`when`(downloadManager).download(any(), anyString())

            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        useCases = DownloadsUseCases(store, mock()),
                        downloadManager = downloadManager,
                        mainDispatcher = testDispatcher,
                        downloadFileUtils = FakeDownloadFileUtils(),
                    )
                )

            doNothing().`when`(feature).showDownloadNotSupportedError()

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            verify(downloadManager, never()).download(any(), anyString())
            verify(feature, never()).showDownloadNotSupportedError()

            val download =
                DownloadState(
                    url = "https://www.mozilla.org",
                    sessionId = "test-tab",
                    directoryPath = "/storage/emulated/0/Download",
                )

            doReturn(false).`when`(feature).isDownloadBiggerThanAvailableSpace(download)

            store.dispatch(ContentAction.UpdateDownloadAction("test-tab", download))

            testDispatcher.scheduler.advanceUntilIdle()

            verify(downloadManager).download(eq(download), anyString())
            verify(feature).showDownloadNotSupportedError()
        }

    @Test
    fun `showDownloadNotSupportedError shows toast`() =
        runTest(testDispatcher) {
            grantPermissions()

            val downloadManager: DownloadManager = mock()
            doReturn(arrayOf(INTERNET, WRITE_EXTERNAL_STORAGE)).`when`(downloadManager).permissions

            doReturn(null).`when`(downloadManager).download(any(), anyString())

            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        useCases = mock(),
                        downloadManager = downloadManager,
                        downloadFileUtils = FakeDownloadFileUtils(),
                    )
                )

            feature.showDownloadNotSupportedError()

            val toast = ShadowToast.getTextOfLatestToast()
            assertNotNull(toast)
            assertTrue(toast.contains("can’t download this file type"))
        }

    @Test
    fun `download dialog must be added once`() =
        runTest(testDispatcher) {
            val fragmentManager = mockFragmentManager()
            val dialog = mock<DownloadDialogFragment>()
            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        useCases = mock(),
                        downloadManager = mock(),
                        fragmentManager = fragmentManager,
                        downloadFileUtils = FakeDownloadFileUtils(),
                    )
                )

            feature.showDownloadDialog(mock(), mock(), dialog)

            verify(dialog).showNow(fragmentManager, DownloadDialogFragment.FRAGMENT_TAG)
            doReturn(true).`when`(feature).isAlreadyADownloadDialog()

            feature.showDownloadDialog(mock(), mock(), dialog)
            verify(dialog, times(1)).showNow(fragmentManager, DownloadDialogFragment.FRAGMENT_TAG)
        }

    @Test
    fun `download dialog must NOT be shown WHEN the fragmentManager isDestroyed`() =
        runTest(testDispatcher) {
            val fragmentManager = mockFragmentManager()
            val dialog = mock<DownloadDialogFragment>()
            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        useCases = mock(),
                        downloadManager = mock(),
                        fragmentManager = fragmentManager,
                        downloadFileUtils = FakeDownloadFileUtils(),
                    )
                )

            doReturn(false).`when`(feature).isAlreadyADownloadDialog()
            doReturn(true).`when`(fragmentManager).isDestroyed

            feature.showDownloadDialog(
                tab = mock(),
                download = mock(),
                dialog = dialog,
            )

            verify(dialog, never()).showNow(fragmentManager, DownloadDialogFragment.FRAGMENT_TAG)
        }

    @Test
    fun `app downloader dialog must NOT be shown WHEN the fragmentManager isDestroyed`() =
        runTest(testDispatcher) {
            val fragmentManager = mockFragmentManager()
            val dialog = mock<DownloadAppChooserDialog>()
            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        useCases = mock(),
                        downloadManager = mock(),
                        downloadFileUtils = FakeDownloadFileUtils(),
                        fragmentManager = fragmentManager,
                    )
                )

            doReturn(false).`when`(feature).isAlreadyADownloadDialog()
            doReturn(true).`when`(fragmentManager).isDestroyed

            feature.showAppDownloaderDialog(mock(), mock(), emptyList(), dialog)

            verify(dialog, never()).showNow(fragmentManager, DownloadDialogFragment.FRAGMENT_TAG)
        }

    @Test
    fun `processDownload only forward downloads when shouldForwardToThirdParties is true`() =
        runTest(testDispatcher) {
            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val download = DownloadState(url = "https://www.mozilla.org/file.txt", sessionId = "test-tab")
            val downloadManager: DownloadManager = mock()

            grantPermissions()

            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        DownloadsUseCases(store, mock()),
                        downloadManager = downloadManager,
                        downloadFileUtils = FakeDownloadFileUtils(),
                        shouldForwardToThirdParties = { false },
                        mainDispatcher = testDispatcher,
                    )
                )

            doReturn(arrayOf(INTERNET, WRITE_EXTERNAL_STORAGE)).`when`(downloadManager).permissions
            doReturn(false).`when`(feature).startDownload(download)

            feature.processDownload(tab, download)

            verify(feature, never()).showAppDownloaderDialog(any(), any(), any(), any())
        }

    @Test
    fun `processDownload must not forward downloads to third party apps when we are the only app that can handle the download`() =
        runTest(testDispatcher) {
            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val download = DownloadState(url = "https://www.mozilla.org/file.txt", sessionId = "test-tab")
            val ourApp = mock<DownloaderApp>()

            grantPermissions()

            val downloadManager: DownloadManager = mock()
            doReturn(arrayOf(INTERNET, WRITE_EXTERNAL_STORAGE)).`when`(downloadManager).permissions

            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        DownloadsUseCases(store, mock()),
                        downloadFileUtils = FakeDownloadFileUtils(),
                        downloadManager = downloadManager,
                        shouldForwardToThirdParties = { true },
                        mainDispatcher = testDispatcher,
                    )
                )

            doReturn(false).`when`(feature).startDownload(download)
            doReturn(listOf(ourApp)).`when`(feature).getDownloaderApps(testContext, download)

            feature.processDownload(tab, download)

            verify(feature, times(0)).showAppDownloaderDialog(any(), any(), any(), any())
        }

    @Test
    fun `processDownload MUST forward downloads to third party apps when there are multiple apps that can handle the download`() =
        runTest(testDispatcher) {
            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val download = DownloadState(url = "https://www.mozilla.org/file.txt", sessionId = "test-tab")
            val ourApp = mock<DownloaderApp>()
            val anotherApp = mock<DownloaderApp>()

            grantPermissions()

            val downloadManager: DownloadManager = mock()
            doReturn(arrayOf(INTERNET, WRITE_EXTERNAL_STORAGE)).`when`(downloadManager).permissions

            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        DownloadsUseCases(store, mock()),
                        downloadFileUtils = FakeDownloadFileUtils(),
                        downloadManager = downloadManager,
                        shouldForwardToThirdParties = { true },
                        mainDispatcher = testDispatcher,
                    )
                )

            doReturn(false).`when`(feature).startDownload(download)
            doNothing().`when`(feature).showAppDownloaderDialog(any(), any(), any(), any())
            doReturn(listOf(ourApp, anotherApp)).`when`(feature).getDownloaderApps(testContext, download)

            feature.processDownload(tab, download)

            verify(feature).showAppDownloaderDialog(any(), any(), any(), any())
        }

    @Test
    fun `GIVEN download should not be forwarded to third party apps but to a custom delegate WHEN processing a download request THEN forward it to the delegate`() =
        runTest(testDispatcher) {
            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val download = DownloadState(url = "https://www.mozilla.org/file.txt", sessionId = "test-tab", id = "test")
            val usecases: DownloadsUseCases = mock()
            val consumeDownloadUseCase: ConsumeDownloadUseCase = mock()
            val cancelDownloadUseCase: CancelDownloadRequestUseCase = mock()
            doReturn(consumeDownloadUseCase).`when`(usecases).consumeDownload
            doReturn(cancelDownloadUseCase).`when`(usecases).cancelDownloadRequest
            val downloadManager: DownloadManager = mock()
            var delegateFilename: String? = ""
            var delegateContentSize: Long? = -1
            var delegatePositiveActionCallback: ((DownloadState) -> Unit)? = null
            var delegateNegativeActionCallback: (() -> Unit)? = null
            grantPermissions()
            doReturn(arrayOf(INTERNET, WRITE_EXTERNAL_STORAGE)).`when`(downloadManager).permissions
            val fakeDownloadFileUtils = FakeDownloadFileUtils(guessFileName = { _, _, _ -> "file.txt" })
            val feature =
                spy(
                    DownloadsFeature(
                        applicationContext = testContext,
                        store = store,
                        useCases = usecases,
                        downloadManager = downloadManager,
                        shouldForwardToThirdParties = { true },
                        downloadFileUtils = fakeDownloadFileUtils,
                        customFirstPartyDownloadDialog = {
                            currentDownloadState,
                            _,
                            positiveActionCallback,
                            negativeActionCallback,
                            _ ->
                            delegateFilename = currentDownloadState.value.fileName
                            delegateContentSize = currentDownloadState.value.contentLength
                            delegatePositiveActionCallback = positiveActionCallback.value
                            delegateNegativeActionCallback = negativeActionCallback.value
                        },
                        mainDispatcher = testDispatcher,
                    )
                )

            doReturn(false).`when`(feature).isDownloadBiggerThanAvailableSpace(download)

            feature.processDownload(tab, download)

            assertEquals("file.txt", delegateFilename)
            assertEquals(0L, delegateContentSize)
            assertNotNull(delegatePositiveActionCallback)
            delegatePositiveActionCallback.invoke(download)
            verify(consumeDownloadUseCase).invoke(tab.id, download.id)
            assertNotNull(delegateNegativeActionCallback)
            delegateNegativeActionCallback.invoke()
            verify(cancelDownloadUseCase).invoke(tab.id, download.id)
        }

    @Test
    fun `GIVEN file with same etag was already downloaded WHEN processing download request THEN the existing file name should be provided to the download dialog`() =
        runTest(testDispatcher) {
            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val download =
                DownloadState(
                    url = "https://www.mozilla.org/file.txt",
                    sessionId = "test-tab",
                    directoryPath = "/downloads",
                    id = "test",
                    etag = "12345",
                )
            val usecases: DownloadsUseCases = mock()
            val consumeDownloadUseCase: ConsumeDownloadUseCase = mock()
            val cancelDownloadUseCase: CancelDownloadRequestUseCase = mock()
            val openAlreadyDownloadedFileUseCase: DownloadsUseCases.OpenAlreadyDownloadedFileUseCase = mock()
            doReturn(consumeDownloadUseCase).`when`(usecases).consumeDownload
            doReturn(cancelDownloadUseCase).`when`(usecases).cancelDownloadRequest
            doReturn(openAlreadyDownloadedFileUseCase).`when`(usecases).openAlreadyDownloadedFile
            val downloadManager: DownloadManager = mock()
            var delegateFilename: String? = ""
            var delegateContentSize: Long? = -1
            var delegateFileNameIsAlreadyDownloaded: String? = null
            var delegatePositiveActionCallback: ((DownloadState) -> Unit)? = null
            var delegateNegativeActionCallback: (() -> Unit)? = null
            var delegateOpenFileCallback: (() -> Unit)? = null
            grantPermissions()
            doReturn(arrayOf(INTERNET, WRITE_EXTERNAL_STORAGE)).`when`(downloadManager).permissions

            val store =
                BrowserStore(
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")),
                        selectedTabId = "test-tab",
                        downloads =
                            mapOf(
                                "test" to
                                    DownloadState(
                                        fileName = "original.txt",
                                        url = "https://www.mozilla.org/file.txt",
                                        directoryPath = "/downloads",
                                        etag = "12345",
                                        status = DownloadState.Status.COMPLETED,
                                    )
                            ),
                    )
                )

            val feature =
                spy(
                    DownloadsFeature(
                        applicationContext = testContext,
                        store = store,
                        useCases = usecases,
                        downloadManager = downloadManager,
                        shouldForwardToThirdParties = { true },
                        downloadFileUtils =
                            FakeDownloadFileUtils(
                                guessFileName = { _, _, _ -> "file.txt" },
                                fileExists = { _, _ -> true },
                            ),
                        customFirstPartyDownloadDialog = {
                            currentDownloadState,
                            fileNameIfAlreadyDownloaded,
                            positiveActionCallback,
                            negativeActionCallback,
                            openFileAction ->
                            delegateFilename = currentDownloadState.value.fileName
                            delegateContentSize = currentDownloadState.value.contentLength
                            delegatePositiveActionCallback = positiveActionCallback.value
                            delegateNegativeActionCallback = negativeActionCallback.value
                            delegateOpenFileCallback = openFileAction.value
                            delegateFileNameIsAlreadyDownloaded = fileNameIfAlreadyDownloaded.value
                        },
                        mainDispatcher = testDispatcher,
                        fileSystemHelper = FakeFileSystemHelper(),
                    )
                )

            doReturn(false).`when`(feature).isDownloadBiggerThanAvailableSpace(download)

            feature.processDownload(tab, download)

            assertEquals("file.txt", delegateFilename)
            assertEquals(0L, delegateContentSize)
            assertEquals("original.txt", delegateFileNameIsAlreadyDownloaded)
            assertNotNull(delegatePositiveActionCallback)
            delegatePositiveActionCallback.invoke(download)
            verify(consumeDownloadUseCase).invoke(tab.id, download.id)
            assertNotNull(delegateNegativeActionCallback)
            delegateNegativeActionCallback.invoke()
            verify(cancelDownloadUseCase).invoke(tab.id, download.id)
            assertNotNull(delegateOpenFileCallback)
            delegateOpenFileCallback.invoke()
            verify(openAlreadyDownloadedFileUseCase).invoke(eq(tab.id), eq(download), eq("/downloads/original.txt"))
        }

    @Test
    fun `GIVEN file to be downloaded for the first time WHEN processing download request THEN the download dialog should be triggered with null existing file`() =
        runTest(testDispatcher) {
            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val download =
                DownloadState(
                    url = "https://www.mozilla.org/file.txt",
                    sessionId = "test-tab",
                    id = "test",
                    etag = "12345",
                )
            val usecases: DownloadsUseCases = mock()
            val consumeDownloadUseCase: ConsumeDownloadUseCase = mock()
            val cancelDownloadUseCase: CancelDownloadRequestUseCase = mock()
            val openAlreadyDownloadedFileUseCase: DownloadsUseCases.OpenAlreadyDownloadedFileUseCase = mock()
            doReturn(consumeDownloadUseCase).`when`(usecases).consumeDownload
            doReturn(cancelDownloadUseCase).`when`(usecases).cancelDownloadRequest
            doReturn(openAlreadyDownloadedFileUseCase).`when`(usecases).openAlreadyDownloadedFile
            val downloadManager: DownloadManager = mock()
            var delegateFilename: String? = ""
            var delegateContentSize: Long? = -1
            var delegateFileNameIsAlreadyDownloaded: String? = null
            grantPermissions()
            doReturn(arrayOf(INTERNET, WRITE_EXTERNAL_STORAGE)).`when`(downloadManager).permissions

            val store =
                BrowserStore(
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")),
                        selectedTabId = "test-tab",
                        downloads = emptyMap(),
                    )
                )
            val fakeDownloadFileUtils = FakeDownloadFileUtils(guessFileName = { _, _, _ -> "file.txt" })
            val feature =
                spy(
                    DownloadsFeature(
                        applicationContext = testContext,
                        store = store,
                        useCases = usecases,
                        downloadManager = downloadManager,
                        mainDispatcher = testDispatcher,
                        shouldForwardToThirdParties = { true },
                        downloadFileUtils = fakeDownloadFileUtils,
                        customFirstPartyDownloadDialog = {
                            currentDownloadState,
                            fileNameIfAlreadyDownloaded,
                            positiveActionCallback,
                            negativeActionCallback,
                            openFileAction ->
                            delegateFilename = currentDownloadState.value.fileName
                            delegateContentSize = currentDownloadState.value.contentLength
                            delegateFileNameIsAlreadyDownloaded = fileNameIfAlreadyDownloaded.value
                        },
                    )
                )

            doReturn(false).`when`(feature).isDownloadBiggerThanAvailableSpace(download)

            feature.processDownload(tab, download)

            assertEquals("file.txt", delegateFilename)
            assertEquals(0L, delegateContentSize)
            assertNull(delegateFileNameIsAlreadyDownloaded)
        }

    @Test
    fun `GIVEN download should be forwarded to third party apps and a custom delegate is set WHEN processing a download request THEN forward it to the delegate`() =
        runTest(testDispatcher) {
            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val download = DownloadState(url = "https://www.mozilla.org/file.txt", sessionId = "test-tab", id = "test")
            val usecases: DownloadsUseCases = mock()
            val cancelDownloadUseCase: CancelDownloadRequestUseCase = mock()
            doReturn(cancelDownloadUseCase).`when`(usecases).cancelDownloadRequest
            val downloadManager: DownloadManager = mock()
            var delegateDownloaderApps: List<DownloaderApp> = emptyList()
            var delegateChosenAppCallback: ((DownloaderApp) -> Unit)? = null
            var delegateNegativeActionCallback: (() -> Unit)? = null
            val ourApp = mock<DownloaderApp>()
            val anotherApp = mock<DownloaderApp>()
            grantPermissions()
            doReturn(arrayOf(INTERNET, WRITE_EXTERNAL_STORAGE)).`when`(downloadManager).permissions
            val feature =
                spy(
                    DownloadsFeature(
                        applicationContext = testContext,
                        store = BrowserStore(),
                        useCases = usecases,
                        downloadManager = downloadManager,
                        mainDispatcher = testDispatcher,
                        shouldForwardToThirdParties = { true },
                        downloadFileUtils = FakeDownloadFileUtils(),
                        customThirdPartyDownloadDialog = { apps, chosenAppCallback, dismissCallback ->
                            delegateDownloaderApps = apps.value
                            delegateChosenAppCallback = chosenAppCallback.value
                            delegateNegativeActionCallback = dismissCallback.value
                        },
                    )
                )
            doReturn(listOf(ourApp, anotherApp)).`when`(feature).getDownloaderApps(testContext, download)
            doNothing().`when`(feature).onDownloaderAppSelected(anotherApp, tab, download)

            feature.processDownload(tab, download)

            assertEquals(listOf(ourApp, anotherApp), delegateDownloaderApps)
            assertNotNull(delegateChosenAppCallback)
            delegateChosenAppCallback.invoke(anotherApp)
            verify(feature).onDownloaderAppSelected(anotherApp, tab, download)
            assertNotNull(delegateNegativeActionCallback)
            delegateNegativeActionCallback.invoke()
            verify(cancelDownloadUseCase).invoke(tab.id, download.id)
        }

    @Test
    @Config(sdk = [32])
    fun `when url is data url return only our app as downloader app on SDK 32 or less`() =
        runTest(testDispatcher) {
            val context = mock<Context>()
            val download = DownloadState(url = "data:", sessionId = "test-tab")
            val app = mock<ResolveInfo>()

            val activityInfo = mock<ActivityInfo>()
            app.activityInfo = activityInfo
            val nonLocalizedLabel = "nonLocalizedLabel"
            val packageName = "packageName"
            val appName = "Fenix"

            activityInfo.packageName = packageName
            activityInfo.name = appName
            activityInfo.exported = true

            val packageManager = mock<PackageManager>()
            whenever(context.packageManager).thenReturn(packageManager)
            whenever(context.packageName).thenReturn(packageName)
            whenever(app.loadLabel(packageManager)).thenReturn(nonLocalizedLabel)

            val ourApp =
                DownloaderApp(
                    nonLocalizedLabel,
                    app,
                    packageName,
                    appName,
                    download.url,
                    download.contentType,
                )

            val mockList = listOf(app)
            @Suppress("DEPRECATION")
            whenever(packageManager.queryIntentActivities(any(), anyInt())).thenReturn(mockList)

            val downloadManager: DownloadManager = mock()

            val feature =
                DownloadsFeature(
                    context,
                    store,
                    DownloadsUseCases(store, mock()),
                    downloadManager = downloadManager,
                    mainDispatcher = testDispatcher,
                    downloadFileUtils = FakeDownloadFileUtils(),
                    shouldForwardToThirdParties = { true },
                )

            val appList = feature.getDownloaderApps(context, download)

            assertTrue(download.url.startsWith("data:"))
            assertEquals(1, appList.size)
            assertEquals(ourApp, appList[0])
        }

    @Test
    fun `when url is data url return only our app as downloader app`() =
        runTest(testDispatcher) {
            val context = mock<Context>()
            val download = DownloadState(url = "data:", sessionId = "test-tab")
            val app = mock<ResolveInfo>()

            val activityInfo = mock<ActivityInfo>()
            app.activityInfo = activityInfo
            val nonLocalizedLabel = "nonLocalizedLabel"
            val packageName = "packageName"
            val appName = "Fenix"

            activityInfo.packageName = packageName
            activityInfo.name = appName
            activityInfo.exported = true

            val packageManager = mock<PackageManager>()
            whenever(context.packageManager).thenReturn(packageManager)
            whenever(context.packageName).thenReturn(packageName)
            whenever(app.loadLabel(packageManager)).thenReturn(nonLocalizedLabel)

            val ourApp =
                DownloaderApp(
                    nonLocalizedLabel,
                    app,
                    packageName,
                    appName,
                    download.url,
                    download.contentType,
                )

            val mockList = listOf(app)

            whenever(
                    packageManager.queryIntentActivities(
                        any(),
                        ArgumentMatchers.any(PackageManager.ResolveInfoFlags::class.java),
                    )
                )
                .thenReturn(mockList)

            val downloadManager: DownloadManager = mock()

            val feature =
                DownloadsFeature(
                    context,
                    store,
                    DownloadsUseCases(store, mock()),
                    downloadManager = downloadManager,
                    mainDispatcher = testDispatcher,
                    shouldForwardToThirdParties = { true },
                    downloadFileUtils = FakeDownloadFileUtils(),
                )

            val appList = feature.getDownloaderApps(context, download)

            assertTrue(download.url.startsWith("data:"))
            assertEquals(1, appList.size)
            assertEquals(ourApp, appList[0])
        }

    @Test
    fun `showAppDownloaderDialog MUST setup and show the dialog`() =
        runTest(testDispatcher) {
            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val download = DownloadState(url = "https://www.mozilla.org/file.txt", sessionId = "test-tab")
            val ourApp = mock<DownloaderApp>()
            val anotherApp = mock<DownloaderApp>()
            val apps = listOf(ourApp, anotherApp)
            val dialog = mock<DownloadAppChooserDialog>()
            val fragmentManager: FragmentManager = mockFragmentManager()
            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        DownloadsUseCases(store, mock()),
                        downloadManager = mock(),
                        mainDispatcher = testDispatcher,
                        shouldForwardToThirdParties = { true },
                        downloadFileUtils = FakeDownloadFileUtils(),
                        fragmentManager = fragmentManager,
                    )
                )

            feature.showAppDownloaderDialog(tab, download, apps, dialog)

            verify(dialog).setApps(apps)
            verify(dialog).onAppSelected = any()
            verify(dialog).onDismiss = any()
            verify(dialog).showNow(fragmentManager, DownloadAppChooserDialog.FRAGMENT_TAG)
        }

    @Test
    fun `WHEN dismissing a downloader app dialog THEN the download should be canceled`() =
        runTest(testDispatcher) {
            val downloadsUseCases = spy(DownloadsUseCases(store, mock()))
            val cancelDownloadRequestUseCase = mock<CancelDownloadRequestUseCase>()
            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val download = DownloadState(url = "https://www.mozilla.org/file.txt", sessionId = "test-tab")
            val ourApp = mock<DownloaderApp>()
            val anotherApp = mock<DownloaderApp>()
            val apps = listOf(ourApp, anotherApp)
            val dialog = spy(DownloadAppChooserDialog())
            val fragmentManager: FragmentManager = mockFragmentManager()
            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        downloadsUseCases,
                        downloadManager = mock(),
                        mainDispatcher = testDispatcher,
                        shouldForwardToThirdParties = { true },
                        downloadFileUtils = FakeDownloadFileUtils(),
                        fragmentManager = fragmentManager,
                    )
                )

            doReturn(cancelDownloadRequestUseCase).`when`(downloadsUseCases).cancelDownloadRequest

            feature.showAppDownloaderDialog(tab, download, apps, dialog)
            dialog.onDismiss()

            verify(cancelDownloadRequestUseCase).invoke(anyString(), anyString())
        }

    @Test
    fun `when isAlreadyAppDownloaderDialog we must NOT show the appChooserDialog`() =
        runTest(testDispatcher) {
            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val download = DownloadState(url = "https://www.mozilla.org/file.txt", sessionId = "test-tab")
            val ourApp = mock<DownloaderApp>()
            val anotherApp = mock<DownloaderApp>()
            val apps = listOf(ourApp, anotherApp)
            val dialog = mock<DownloadAppChooserDialog>()
            val fragmentManager: FragmentManager = mockFragmentManager()
            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        DownloadsUseCases(store, mock()),
                        downloadManager = mock(),
                        mainDispatcher = testDispatcher,
                        shouldForwardToThirdParties = { true },
                        downloadFileUtils = FakeDownloadFileUtils(),
                        fragmentManager = fragmentManager,
                    )
                )

            doReturn(dialog).`when`(fragmentManager).findFragmentByTag(DownloadAppChooserDialog.FRAGMENT_TAG)
            doReturn(true).`when`(feature).isAlreadyAppDownloaderDialog()

            feature.showAppDownloaderDialog(tab, download, apps)

            verify(dialog).setApps(apps)
            verify(dialog).onAppSelected = any()
            verify(dialog).onDismiss = any()
            verify(dialog, times(0)).showNow(fragmentManager, DownloadAppChooserDialog.FRAGMENT_TAG)
        }

    @Test
    fun `when our app is selected for downloading and permission granted then we should perform the download`() =
        runTest(testDispatcher) {
            val spyContext = spy(testContext)
            val downloadsUseCases = spy(DownloadsUseCases(store, mock()))
            val consumeDownloadUseCase = mock<ConsumeDownloadUseCase>()
            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val download = DownloadState(url = "https://www.mozilla.org/file.txt", sessionId = "test-tab")
            val ourApp =
                DownloaderApp(
                    name = "app",
                    packageName = testContext.packageName,
                    resolver = mock(),
                    activityName = "",
                    url = "",
                    contentType = null,
                )
            val anotherApp = mock<DownloaderApp>()
            val apps = listOf(ourApp, anotherApp)
            val dialog = DownloadAppChooserDialog()
            val fragmentManager: FragmentManager = mockFragmentManager()
            val downloadManager: DownloadManager = mock()
            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        downloadsUseCases,
                        downloadManager = downloadManager,
                        mainDispatcher = testDispatcher,
                        downloadFileUtils = FakeDownloadFileUtils(),
                        shouldForwardToThirdParties = { true },
                        fragmentManager = fragmentManager,
                    )
                )

            grantPermissions()

            doReturn(dialog).`when`(fragmentManager).findFragmentByTag(DownloadAppChooserDialog.FRAGMENT_TAG)
            doReturn(consumeDownloadUseCase).`when`(downloadsUseCases).consumeDownload
            doReturn(false).`when`(feature).startDownload(any())
            doReturn(arrayOf(INTERNET, WRITE_EXTERNAL_STORAGE)).`when`(downloadManager).permissions

            feature.showAppDownloaderDialog(tab, download, apps)
            dialog.onAppSelected(ourApp)

            verify(feature).startDownload(any())
            verify(consumeDownloadUseCase).invoke(anyString(), anyString())
            verify(spyContext, times(0)).startActivity(any())
        }

    @Test
    fun `GIVEN permissions are granted WHEN our app is selected for download THEN perform the download`() =
        runTest(testDispatcher) {
            val spyContext = spy(testContext)
            val usecases: DownloadsUseCases = mock()
            val consumeDownloadUseCase: ConsumeDownloadUseCase = mock()
            doReturn(consumeDownloadUseCase).`when`(usecases).consumeDownload
            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val download = DownloadState(url = "https://www.mozilla.org/file.txt", sessionId = "test-tab", id = "test")
            val ourApp =
                DownloaderApp(
                    name = "app",
                    packageName = testContext.packageName,
                    resolver = mock(),
                    activityName = "",
                    url = "",
                    contentType = null,
                )
            var wasPermissionsRequested = false
            val feature =
                spy(
                    DownloadsFeature(
                        applicationContext = testContext,
                        store = BrowserStore(),
                        useCases = usecases,
                        mainDispatcher = testDispatcher,
                        downloadFileUtils = FakeDownloadFileUtils(),
                        onNeedToRequestPermissions = { wasPermissionsRequested = true },
                    )
                )
            doReturn(false).`when`(feature).startDownload(any())

            grantPermissions()
            feature.onDownloaderAppSelected(ourApp, tab, download)

            verify(feature).startDownload(download)
            verify(consumeDownloadUseCase).invoke(tab.id, download.id)
            assertFalse(wasPermissionsRequested)
            verify(spyContext, never()).startActivity(any())
        }

    @Test
    fun `GIVEN permissions are not granted WHEN our app is selected for download THEN request the needed permissions`() =
        runTest(testDispatcher) {
            val spyContext = spy(testContext)
            val usecases: DownloadsUseCases = mock()
            val consumeDownloadUseCase: ConsumeDownloadUseCase = mock()
            doReturn(consumeDownloadUseCase).`when`(usecases).consumeDownload
            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val download = DownloadState(url = "https://www.mozilla.org/file.txt", sessionId = "test-tab", id = "test")
            val ourApp =
                DownloaderApp(
                    name = "app",
                    packageName = testContext.packageName,
                    resolver = mock(),
                    activityName = "",
                    url = "",
                    contentType = null,
                )
            var wasPermissionsRequested = false
            val feature =
                spy(
                    DownloadsFeature(
                        applicationContext = testContext,
                        store = BrowserStore(),
                        useCases = usecases,
                        mainDispatcher = testDispatcher,
                        downloadFileUtils = FakeDownloadFileUtils(),
                        onNeedToRequestPermissions = { wasPermissionsRequested = true },
                    )
                )

            feature.onDownloaderAppSelected(ourApp, tab, download)

            verify(feature, never()).startDownload(any())
            verify(consumeDownloadUseCase, never()).invoke(anyString(), anyString())
            assertTrue(wasPermissionsRequested)
            verify(spyContext, never()).startActivity(any())
        }

    @Test
    fun `GIVEN a download WHEN a 3rd party app is selected THEN delegate download to it`() =
        runTest(testDispatcher) {
            val spyContext = spy(testContext)
            doNothing().`when`(spyContext).startActivity(any())
            val usecases: DownloadsUseCases = mock()
            val consumeDownloadUseCase: ConsumeDownloadUseCase = mock()
            doReturn(consumeDownloadUseCase).`when`(usecases).consumeDownload
            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val download = DownloadState(url = "https://www.mozilla.org/file.txt", sessionId = "test-tab", id = "test")
            val anotherApp =
                DownloaderApp(
                    name = "app",
                    packageName = "test",
                    resolver = mock(),
                    activityName = "",
                    url = download.url,
                    contentType = null,
                )
            val feature =
                spy(
                    DownloadsFeature(
                        applicationContext = spyContext,
                        store = BrowserStore(),
                        downloadFileUtils = FakeDownloadFileUtils(),
                        useCases = usecases,
                        mainDispatcher = testDispatcher,
                    )
                )
            val intentArgumentCaptor = argumentCaptor<Intent>()
            val expectedIntent = with(feature) { anotherApp.toIntent() }

            feature.onDownloaderAppSelected(anotherApp, tab, download)

            verify(spyContext).startActivity(intentArgumentCaptor.capture())
            assertEquals(expectedIntent.toUri(0), intentArgumentCaptor.value.toUri(0))
            verify(consumeDownloadUseCase).invoke(tab.id, download.id)
            verify(feature, never()).startDownload(any())
            assertNull(ShadowToast.getTextOfLatestToast())
        }

    @Test
    fun `GIVEN a download WHEN a 3rd party app is selected and the download fails THEN show a warning toast and consume the download`() =
        runTest(testDispatcher) {
            val spyContext = spy(testContext)
            val usecases: DownloadsUseCases = mock()
            val consumeDownloadUseCase: ConsumeDownloadUseCase = mock()
            doReturn(consumeDownloadUseCase).`when`(usecases).consumeDownload
            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val download = DownloadState(url = "https://www.mozilla.org/file.txt", sessionId = "test-tab", id = "test")
            val anotherApp =
                DownloaderApp(
                    name = "app",
                    packageName = "test",
                    resolver = mock(),
                    activityName = "",
                    url = download.url,
                    contentType = null,
                )
            val feature =
                spy(
                    DownloadsFeature(
                        applicationContext = spyContext,
                        store = BrowserStore(),
                        useCases = usecases,
                        mainDispatcher = testDispatcher,
                        downloadFileUtils = FakeDownloadFileUtils(),
                    )
                )
            val expectedWarningText =
                testContext.getString(
                    R.string.mozac_feature_downloads_unable_to_open_third_party_app,
                    anotherApp.name,
                )
            val intentArgumentCaptor = argumentCaptor<Intent>()
            val expectedIntent = with(feature) { anotherApp.toIntent() }
            doThrow(ActivityNotFoundException()).`when`(spyContext).startActivity(any())

            feature.onDownloaderAppSelected(anotherApp, tab, download)

            verify(spyContext).startActivity(intentArgumentCaptor.capture())
            assertEquals(expectedIntent.toUri(0), intentArgumentCaptor.value.toUri(0))
            verify(consumeDownloadUseCase).invoke(tab.id, download.id)
            verify(feature, never()).startDownload(any())
            assertEquals(expectedWarningText, ShadowToast.getTextOfLatestToast())
        }

    @Test
    fun `when an app third party is selected for downloading we MUST forward the download`() =
        runTest(testDispatcher) {
            val spyContext = spy(testContext)
            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val downloadsUseCases = spy(DownloadsUseCases(store, mock()))
            val consumeDownloadUseCase = mock<ConsumeDownloadUseCase>()
            val download = DownloadState(url = "https://www.mozilla.org/file.txt", sessionId = "test-tab")
            val ourApp =
                DownloaderApp(
                    name = "app",
                    packageName = "thridparty.app",
                    resolver = mock(),
                    activityName = "",
                    url = "",
                    contentType = null,
                )
            val anotherApp = mock<DownloaderApp>()
            val apps = listOf(ourApp, anotherApp)
            val dialog = DownloadAppChooserDialog()
            val fragmentManager: FragmentManager = mockFragmentManager()
            val feature =
                spy(
                    DownloadsFeature(
                        spyContext,
                        store,
                        downloadsUseCases,
                        downloadManager = mock(),
                        shouldForwardToThirdParties = { true },
                        downloadFileUtils = FakeDownloadFileUtils(),
                        fragmentManager = fragmentManager,
                        mainDispatcher = testDispatcher,
                    )
                )

            doReturn(false).`when`(feature).startDownload(any())
            doReturn(dialog).`when`(fragmentManager).findFragmentByTag(DownloadAppChooserDialog.FRAGMENT_TAG)
            doReturn(consumeDownloadUseCase).`when`(downloadsUseCases).consumeDownload

            feature.showAppDownloaderDialog(tab, download, apps)
            dialog.onAppSelected(ourApp)

            verify(feature, times(0)).startDownload(any())
            verify(consumeDownloadUseCase).invoke(anyString(), anyString())
            verify(spyContext).startActivity(any())
        }

    @Test
    fun `None exception is thrown when unable to open an app third party for downloading`() =
        runTest(testDispatcher) {
            val spyContext = spy(testContext)
            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val downloadsUseCases = spy(DownloadsUseCases(store, mock()))
            val consumeDownloadUseCase = mock<ConsumeDownloadUseCase>()
            val download = DownloadState(url = "https://www.mozilla.org/file.txt", sessionId = "test-tab")
            val ourApp =
                DownloaderApp(
                    name = "app",
                    packageName = "thridparty.app",
                    resolver = mock(),
                    activityName = "",
                    url = "",
                    contentType = null,
                )
            val anotherApp = mock<DownloaderApp>()
            val apps = listOf(ourApp, anotherApp)
            val dialog = DownloadAppChooserDialog()
            val fragmentManager: FragmentManager = mockFragmentManager()
            val feature =
                spy(
                    DownloadsFeature(
                        spyContext,
                        store,
                        downloadsUseCases,
                        downloadManager = mock(),
                        shouldForwardToThirdParties = { true },
                        downloadFileUtils = FakeDownloadFileUtils(),
                        fragmentManager = fragmentManager,
                        mainDispatcher = testDispatcher,
                    )
                )

            doThrow(ActivityNotFoundException()).`when`(spyContext).startActivity(any())
            doReturn(false).`when`(feature).startDownload(any())
            doReturn(dialog).`when`(fragmentManager).findFragmentByTag(DownloadAppChooserDialog.FRAGMENT_TAG)
            doReturn(consumeDownloadUseCase).`when`(downloadsUseCases).consumeDownload

            feature.showAppDownloaderDialog(tab, download, apps)
            dialog.onAppSelected(ourApp)

            verify(feature, times(0)).startDownload(any())
            verify(consumeDownloadUseCase).invoke(anyString(), anyString())
            verify(spyContext).startActivity(any())
        }

    @Test
    fun `when the appChooserDialog is dismissed THEN the download must be canceled`() =
        runTest(testDispatcher) {
            val spyContext = spy(testContext)
            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val downloadsUseCases = spy(DownloadsUseCases(store, mock()))
            val cancelDownloadRequestUseCase = mock<CancelDownloadRequestUseCase>()
            val download = DownloadState(url = "https://www.mozilla.org/file.txt", sessionId = "test-tab")
            val ourApp = mock<DownloaderApp>()
            val anotherApp = mock<DownloaderApp>()
            val apps = listOf(ourApp, anotherApp)
            val dialog = DownloadAppChooserDialog()
            val fragmentManager: FragmentManager = mockFragmentManager()
            val feature =
                spy(
                    DownloadsFeature(
                        spyContext,
                        store,
                        downloadsUseCases,
                        downloadManager = mock(),
                        shouldForwardToThirdParties = { true },
                        downloadFileUtils = FakeDownloadFileUtils(),
                        fragmentManager = fragmentManager,
                        mainDispatcher = testDispatcher,
                    )
                )

            doReturn(false).`when`(feature).startDownload(any())
            doReturn(dialog).`when`(fragmentManager).findFragmentByTag(DownloadAppChooserDialog.FRAGMENT_TAG)
            doReturn(cancelDownloadRequestUseCase).`when`(downloadsUseCases).cancelDownloadRequest

            feature.showAppDownloaderDialog(tab, download, apps)
            dialog.onDismiss()

            verify(cancelDownloadRequestUseCase).invoke(anyString(), anyString())
        }

    @Test
    fun `GIVEN a custom download dialog is used WHEN dismissAllDownloadDialogs is called THEN the dialog is dismissed`() =
        runTest(testDispatcher) {
            val dismissCustomDialog = mock<() -> Unit>()
            val feature =
                DownloadsFeature(
                    testContext,
                    store,
                    useCases = DownloadsUseCases(store, mock()),
                    downloadFileUtils = FakeDownloadFileUtils(),
                    downloadManager = mock(),
                    mainDispatcher = testDispatcher,
                    dismissCustomFirstPartyDownloadDialog = dismissCustomDialog,
                )

            feature.dismissAllDownloadDialogs()

            verify(dismissCustomDialog).invoke()
        }

    @Test
    fun `GIVEN a custom download dialog is used WHEN navigating to another website THEN the dialog is dismissed`() =
        runTest(testDispatcher) {
            val dismissCustomDialog = mock<() -> Unit>()
            val downloadsUseCases = spy(DownloadsUseCases(store, mock()))
            val cancelDownloadRequestUseCase = mock<CancelDownloadRequestUseCase>()
            val download = DownloadState(url = "https://www.mozilla.org", sessionId = "test-tab")
            store.dispatch(ContentAction.UpdateDownloadAction("test-tab", download = download))

            doReturn(cancelDownloadRequestUseCase).`when`(downloadsUseCases).cancelDownloadRequest

            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        useCases = downloadsUseCases,
                        downloadFileUtils = FakeDownloadFileUtils(),
                        downloadManager = mock(),
                        mainDispatcher = testDispatcher,
                        dismissCustomFirstPartyDownloadDialog = dismissCustomDialog,
                    )
                )

            doReturn(true).`when`(feature).processDownload(any(), any())

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            store.dispatch(ContentAction.UpdateDownloadAction("test-tab", download = download))
            testDispatcher.scheduler.advanceUntilIdle()

            grantPermissions()

            val tab = createTab("https://www.firefox.com")
            store.dispatch(TabListAction.AddTabAction(tab, select = true))
            testDispatcher.scheduler.advanceUntilIdle()

            verify(feature).dismissAllDownloadDialogs()
            verify(dismissCustomDialog).invoke()
        }

    @Test
    fun `ResolveInfo to DownloaderApps`() =
        runTest(testDispatcher) {
            val spyContext = spy(testContext)
            val download = DownloadState(url = "https://www.mozilla.org/file.txt", sessionId = "test-tab")
            val info =
                ActivityInfo().apply {
                    packageName = "thridparty.app"
                    name = "activityName"
                    icon = android.R.drawable.btn_default
                }
            val resolveInfo =
                ResolveInfo().apply {
                    labelRes = android.R.string.ok
                    activityInfo = info
                    nonLocalizedLabel = "app"
                }

            val expectedApp =
                DownloaderApp(
                    name = "app",
                    packageName = "thridparty.app",
                    resolver = resolveInfo,
                    activityName = "activityName",
                    url = download.url,
                    contentType = download.contentType,
                )

            val app = resolveInfo.toDownloaderApp(spyContext, download)
            assertEquals(expectedApp, app)
        }

    @Test
    fun `previous dialogs MUST be dismissed when navigating to another website`() =
        runTest(testDispatcher) {
            val downloadsUseCases = spy(DownloadsUseCases(store, mock()))
            val cancelDownloadRequestUseCase = mock<CancelDownloadRequestUseCase>()
            val download = DownloadState(url = "https://www.mozilla.org", sessionId = "test-tab")
            store.dispatch(ContentAction.UpdateDownloadAction("test-tab", download = download))

            doReturn(cancelDownloadRequestUseCase).`when`(downloadsUseCases).cancelDownloadRequest

            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        useCases = downloadsUseCases,
                        downloadFileUtils = FakeDownloadFileUtils(),
                        downloadManager = mock(),
                        mainDispatcher = testDispatcher,
                    )
                )

            doNothing().`when`(feature).dismissAllDownloadDialogs()
            doReturn(true).`when`(feature).processDownload(any(), any())

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            store.dispatch(ContentAction.UpdateDownloadAction("test-tab", download = download))
            testDispatcher.scheduler.advanceUntilIdle()

            grantPermissions()

            val tab = createTab("https://www.firefox.com")
            store.dispatch(TabListAction.AddTabAction(tab, select = true))
            testDispatcher.scheduler.advanceUntilIdle()

            verify(feature).dismissAllDownloadDialogs()
            verify(downloadsUseCases).cancelDownloadRequest
            assertNull(feature.previousTab)
        }

    @Test
    fun `previous dialogs must NOT be dismissed when navigating on the same website`() =
        runTest(testDispatcher) {
            val downloadsUseCases = spy(DownloadsUseCases(store, mock()))
            val cancelDownloadRequestUseCase = mock<CancelDownloadRequestUseCase>()
            val download = DownloadState(url = "https://www.mozilla.org", sessionId = "test-tab")
            store.dispatch(ContentAction.UpdateDownloadAction("test-tab", download = download))

            doReturn(cancelDownloadRequestUseCase).`when`(downloadsUseCases).cancelDownloadRequest

            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        useCases = downloadsUseCases,
                        downloadFileUtils = FakeDownloadFileUtils(),
                        downloadManager = mock(),
                        mainDispatcher = testDispatcher,
                    )
                )

            doNothing().`when`(feature).dismissAllDownloadDialogs()
            doReturn(true).`when`(feature).processDownload(any(), any())

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            store.dispatch(ContentAction.UpdateDownloadAction("test-tab", download = download))
            testDispatcher.scheduler.advanceUntilIdle()

            grantPermissions()

            val tab = createTab("https://www.mozilla.org/example")
            store.dispatch(TabListAction.AddTabAction(tab, select = true))
            testDispatcher.scheduler.advanceUntilIdle()

            verify(feature, never()).dismissAllDownloadDialogs()
            verify(downloadsUseCases, never()).cancelDownloadRequest
            assertNotNull(feature.previousTab)
        }

    @Test
    fun `when our app is selected for downloading and permission not granted then we should ask for permission`() =
        runTest(testDispatcher) {
            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val download = DownloadState(url = "https://www.mozilla.org/file.txt", sessionId = "test-tab")
            val ourApp =
                DownloaderApp(
                    name = "app",
                    packageName = testContext.packageName,
                    resolver = mock(),
                    activityName = "",
                    url = "",
                    contentType = null,
                )
            val anotherApp = mock<DownloaderApp>()
            val apps = listOf(ourApp, anotherApp)
            val downloadManager: DownloadManager = mock()
            var permissionsRequested = false
            val dialog = DownloadAppChooserDialog()
            val downloadsUseCases = spy(DownloadsUseCases(store, mock()))
            val consumeDownloadUseCase = mock<ConsumeDownloadUseCase>()
            val fragmentManager: FragmentManager = mockFragmentManager()

            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        useCases = downloadsUseCases,
                        downloadManager = downloadManager,
                        shouldForwardToThirdParties = { true },
                        onNeedToRequestPermissions = { permissionsRequested = true },
                        downloadFileUtils = FakeDownloadFileUtils(),
                        fragmentManager = fragmentManager,
                        mainDispatcher = testDispatcher,
                    )
                )

            doReturn(arrayOf(INTERNET, WRITE_EXTERNAL_STORAGE)).`when`(downloadManager).permissions
            doReturn(dialog).`when`(fragmentManager).findFragmentByTag(DownloadAppChooserDialog.FRAGMENT_TAG)
            doReturn(consumeDownloadUseCase).`when`(downloadsUseCases).consumeDownload

            assertFalse(permissionsRequested)

            feature.showAppDownloaderDialog(tab, download, apps, dialog)
            dialog.onAppSelected(ourApp)

            assertTrue(permissionsRequested)

            verify(feature, never()).startDownload(any())
            verify(spy(testContext), never()).startActivity(any())
            verify(consumeDownloadUseCase, never()).invoke(anyString(), anyString())
        }

    @Test
    fun `GIVEN phone storage is full WHEN our app is selected for download THEN show not enough storage dialog`() =
        runTest(testDispatcher) {
            val downloadsUseCases: DownloadsUseCases = mock()
            val cancelDownloadRequestUseCase = mock<CancelDownloadRequestUseCase>()
            val consumeDownloadUseCase: ConsumeDownloadUseCase = mock()
            val fileHasNotEnoughStorageDialog: ((Filename) -> Unit) = mock()

            doReturn(consumeDownloadUseCase).`when`(downloadsUseCases).consumeDownload

            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val download =
                DownloadState(
                    url = "https://www.mozilla.org/file.txt",
                    sessionId = "test-tab",
                    id = "test",
                    fileName = "file.txt",
                )
            val ourApp =
                DownloaderApp(
                    name = "app",
                    packageName = testContext.packageName,
                    resolver = mock(),
                    activityName = "",
                    url = "",
                    contentType = null,
                )
            val feature =
                spy(
                    DownloadsFeature(
                        applicationContext = testContext,
                        store = BrowserStore(),
                        useCases = downloadsUseCases,
                        fileHasNotEnoughStorageDialog = fileHasNotEnoughStorageDialog,
                        mainDispatcher = testDispatcher,
                        downloadFileUtils = FakeDownloadFileUtils(),
                    )
                )

            doReturn(cancelDownloadRequestUseCase).`when`(downloadsUseCases).cancelDownloadRequest

            doReturn(true).`when`(feature).isDownloadBiggerThanAvailableSpace(download)

            grantPermissions()

            feature.onDownloaderAppSelected(ourApp, tab, download)

            verify(fileHasNotEnoughStorageDialog).invoke(Filename("file.txt"))
            verify(downloadsUseCases).cancelDownloadRequest
            assertFalse(feature.startDownload(download))
        }

    @Test
    fun `GIVEN file with same ETag was already downloaded WHEN starting download THEN show call download dialog with the already downloaded file name`() =
        runTest(testDispatcher) {
            val downloadsUseCases: DownloadsUseCases = mock()
            val consumeDownloadUseCase: ConsumeDownloadUseCase = mock()
            val openAlreadyDownloadedFileUseCase: DownloadsUseCases.OpenAlreadyDownloadedFileUseCase = mock()
            var fileNameIfAlreadyDownloaded: String? = null
            var delegateOpenFileCallback: (() -> Unit)? = null

            doReturn(consumeDownloadUseCase).`when`(downloadsUseCases).consumeDownload
            doReturn(openAlreadyDownloadedFileUseCase).`when`(downloadsUseCases).openAlreadyDownloadedFile

            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val download =
                DownloadState(
                    url = "https://www.mozilla.org/file.txt",
                    sessionId = "test-tab",
                    id = "test",
                    fileName = "file.txt",
                    etag = "12345",
                    directoryPath = "/downloads",
                )
            val store =
                BrowserStore(
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")),
                        selectedTabId = "test-tab",
                        downloads = mapOf("test" to download.copy(status = DownloadState.Status.COMPLETED)),
                    )
                )
            val feature =
                spy(
                    DownloadsFeature(
                        applicationContext = testContext,
                        store = store,
                        useCases = downloadsUseCases,
                        mainDispatcher = testDispatcher,
                        customFirstPartyDownloadDialog = { currentDownloadState, _, _, _, openFileAction ->
                            fileNameIfAlreadyDownloaded = currentDownloadState.value.fileName
                            delegateOpenFileCallback = openFileAction.value
                        },
                        fileSystemHelper = FakeFileSystemHelper(),
                        downloadFileUtils = FakeDownloadFileUtils(fileExists = { _, _ -> true }),
                    )
                )

            grantPermissions()

            feature.processDownload(tab, download)

            assertEquals(fileNameIfAlreadyDownloaded, "file.txt")

            delegateOpenFileCallback?.invoke()
            verify(openAlreadyDownloadedFileUseCase).invoke(eq(tab.id), eq(download), eq("/downloads/file.txt"))
        }

    @Test
    fun `GIVEN file is downloaded for the first time WHEN starting download THEN call download dialog with no alreadyDownloadedFile`() =
        runTest(testDispatcher) {
            val downloadsUseCases: DownloadsUseCases = mock()
            val consumeDownloadUseCase: ConsumeDownloadUseCase = mock()
            var fileNameIfAlreadyDownloaded: String? = null

            doReturn(consumeDownloadUseCase).`when`(downloadsUseCases).consumeDownload

            val tab = createTab("https://www.mozilla.org", id = "test-tab")
            val download =
                DownloadState(
                    url = "https://www.mozilla.org/file.txt",
                    sessionId = "test-tab",
                    id = "test",
                    fileName = "file.txt",
                    etag = "12345",
                    directoryPath = "/downloads",
                )
            val store =
                BrowserStore(
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")),
                        selectedTabId = "test-tab",
                        downloads = emptyMap(),
                    )
                )
            val feature =
                spy(
                    DownloadsFeature(
                        applicationContext = testContext,
                        store = store,
                        useCases = downloadsUseCases,
                        mainDispatcher = testDispatcher,
                        customFirstPartyDownloadDialog = {
                            _,
                            fileNameOfDuplicateIfAlreadyDownloaded,
                            _,
                            _,
                            openFileAction ->
                            fileNameIfAlreadyDownloaded = fileNameOfDuplicateIfAlreadyDownloaded.value
                        },
                        downloadFileUtils = FakeDownloadFileUtils(),
                    )
                )

            grantPermissions()

            feature.processDownload(tab, download)
            assertNull(fileNameIfAlreadyDownloaded)
        }

    @Test
    fun `GIVEN content length is 0L WHEN calling isDownloadBiggerThanAvailableSpace THEN it returns false`() =
        runTest(testDispatcher) {
            val directoryPath = "/valid/path"

            val feature =
                spy(
                    DownloadsFeature(
                        applicationContext = testContext,
                        store = BrowserStore(),
                        useCases = mock(),
                        mainDispatcher = testDispatcher,
                        fileSystemHelper =
                            FakeFileSystemHelper(
                                availableBitesInDirectory = 10L,
                                existingDirectories = listOf(directoryPath),
                            ),
                        downloadFileUtils = FakeDownloadFileUtils(),
                    )
                )

            val downloadState =
                DownloadState(
                    id = "test_id",
                    url = "test_url",
                    fileName = "test_file",
                    directoryPath = directoryPath,
                )

            assertFalse(feature.isDownloadBiggerThanAvailableSpace(downloadState))
        }

    @Test
    fun `GIVEN download is bigger than available space WHEN calling isDownloadBiggerThanAvailableSpace THEN it returns true`() =
        runTest(testDispatcher) {
            val directoryPath = "/valid/path"

            val feature =
                spy(
                    DownloadsFeature(
                        applicationContext = testContext,
                        store = BrowserStore(),
                        useCases = mock(),
                        mainDispatcher = testDispatcher,
                        fileSystemHelper =
                            FakeFileSystemHelper(
                                availableBitesInDirectory = 10L,
                                existingDirectories = listOf(directoryPath),
                            ),
                        downloadFileUtils = FakeDownloadFileUtils(),
                    )
                )

            val downloadState =
                DownloadState(
                    id = "test_id",
                    url = "test_url",
                    fileName = "test_file",
                    directoryPath = directoryPath,
                    contentLength = 1000L,
                )

            assertTrue(feature.isDownloadBiggerThanAvailableSpace(downloadState))
        }

    @Test
    fun `GIVEN download is smaller than available space WHEN calling isDownloadBiggerThanAvailableSpace THEN it returns false`() =
        runTest(testDispatcher) {
            val directoryPath = "/valid/path"

            val feature =
                spy(
                    DownloadsFeature(
                        applicationContext = testContext,
                        store = BrowserStore(),
                        useCases = mock(),
                        mainDispatcher = testDispatcher,
                        fileSystemHelper =
                            FakeFileSystemHelper(
                                availableBitesInDirectory = 1000L,
                                existingDirectories = listOf(directoryPath),
                            ),
                        downloadFileUtils = FakeDownloadFileUtils(),
                    )
                )
            val downloadState =
                DownloadState(
                    id = "test_id",
                    url = "test_url",
                    fileName = "test_file",
                    directoryPath = directoryPath,
                    contentLength = 100L,
                )

            assertFalse(feature.isDownloadBiggerThanAvailableSpace(downloadState))
        }

    @Test
    fun `GIVEN download directory doesn't exist WHEN calling isDownloadBiggerThanAvailableSpace THEN it returns false`() =
        runTest(testDispatcher) {
            val feature =
                spy(
                    DownloadsFeature(
                        applicationContext = testContext,
                        store = BrowserStore(),
                        useCases = mock(),
                        mainDispatcher = testDispatcher,
                        fileSystemHelper =
                            FakeFileSystemHelper(
                                availableBitesInDirectory = 10L,
                                existingDirectories = emptyList(),
                            ),
                        downloadFileUtils = FakeDownloadFileUtils(),
                    )
                )
            val downloadState =
                DownloadState(
                    id = "test_id",
                    url = "test_url",
                    fileName = "test_file",
                    directoryPath = "/invalid/path",
                    contentLength = 100L,
                )

            assertFalse(feature.isDownloadBiggerThanAvailableSpace(downloadState))
        }

    @Test
    fun `WHEN download has started with success THEN call onDownloadStartedListener`() =
        runTest(testDispatcher) {
            grantPermissions()

            val downloadManager: DownloadManager = mock()
            val onDownloadStartedListener: ((String?) -> Unit) = mock()

            doReturn(arrayOf(INTERNET, WRITE_EXTERNAL_STORAGE)).`when`(downloadManager).permissions

            val download = DownloadState(url = "https://www.mozilla.org", sessionId = "test-tab", id = "downloadId")

            doReturn("id").`when`(downloadManager).download(eq(download), anyString())

            val feature =
                spy(
                    DownloadsFeature(
                        testContext,
                        store,
                        useCases = DownloadsUseCases(store, mock()),
                        tabId = "id",
                        downloadManager = downloadManager,
                        onDownloadStartedListener = onDownloadStartedListener,
                        mainDispatcher = testDispatcher,
                        downloadFileUtils = FakeDownloadFileUtils(),
                    )
                )

            doNothing().`when`(feature).showDownloadNotSupportedError()

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            doReturn(false).`when`(feature).isDownloadBiggerThanAvailableSpace(download)

            feature.startDownload(download)

            verify(onDownloadStartedListener).invoke("downloadId")
        }

    @Test
    fun `WHEN file was already downloaded with same etag and url THEN findDownloadWithSameEtag returns the previous download`() =
        runTest(testDispatcher) {
            val download =
                DownloadState(
                    url = "https://www.mozilla.org/file.txt",
                    sessionId = "test-tab",
                    id = "test",
                    fileName = "file.txt",
                    etag = "12345",
                    directoryPath = "/downloads",
                )
            val previousDownload =
                DownloadState(
                    url = "https://www.mozilla.org/file.txt",
                    fileName = "previous.txt",
                    directoryPath = "/downloads",
                    etag = "12345",
                    status = DownloadState.Status.COMPLETED,
                )
            val store =
                BrowserStore(
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")),
                        selectedTabId = "test-tab",
                        downloads = mapOf("test" to previousDownload),
                    )
                )
            val feature =
                DownloadsFeature(
                    applicationContext = testContext,
                    store = store,
                    useCases = DownloadsUseCases(store, mock()),
                    mainDispatcher = testDispatcher,
                    fileSystemHelper = FakeFileSystemHelper(),
                    downloadFileUtils = FakeDownloadFileUtils(fileExists = { _, _ -> true }),
                )

            val foundDownload = feature.findDownloadWithSameEtag(download)

            assertEquals(previousDownload, foundDownload)
        }

    @Test
    fun `WHEN file was already downloaded several times with same etag and url THEN findDownloadWithSameEtag returns the oldest download`() =
        runTest(testDispatcher) {
            val download =
                DownloadState(
                    url = "https://www.mozilla.org/file.txt",
                    sessionId = "test-tab",
                    id = "test",
                    fileName = "file.txt",
                    directoryPath = "/downloads",
                    etag = "12345",
                )
            val previousDownloadOldest =
                DownloadState(
                    url = "https://www.mozilla.org/file.txt",
                    id = "oldest",
                    fileName = "previous.txt",
                    directoryPath = "/downloads",
                    etag = "12345",
                    status = DownloadState.Status.COMPLETED,
                    createdTime = 0,
                )
            val previousDownloadNewest =
                DownloadState(
                    url = "https://www.mozilla.org/file.txt",
                    id = "newest",
                    fileName = "previous(1).txt",
                    directoryPath = "/downloads",
                    etag = "12345",
                    status = DownloadState.Status.COMPLETED,
                    createdTime = 1,
                )
            val store =
                BrowserStore(
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")),
                        selectedTabId = "test-tab",
                        downloads = mapOf("newest" to previousDownloadNewest, "oldest" to previousDownloadOldest),
                    )
                )
            val feature =
                spy(
                    DownloadsFeature(
                        applicationContext = testContext,
                        store = store,
                        useCases = DownloadsUseCases(store, mock()),
                        fileSystemHelper = FakeFileSystemHelper(),
                        mainDispatcher = testDispatcher,
                        downloadFileUtils = FakeDownloadFileUtils(fileExists = { _, _ -> true }),
                    )
                )

            val foundDownload = feature.findDownloadWithSameEtag(download)

            assertEquals(previousDownloadOldest, foundDownload)
        }

    @Test
    fun `WHEN file is already being downloaded - IN PROGRESS - with same etag and url THEN findDownloadWithSameEtag returns null`() =
        runTest(testDispatcher) {
            val download =
                DownloadState(
                    url = "https://www.mozilla.org/file.txt",
                    sessionId = "test-tab",
                    id = "test",
                    fileName = "file.txt",
                    etag = "12345",
                    directoryPath = "/downloads",
                )
            val previousDownload =
                DownloadState(
                    url = "https://www.mozilla.org/file.txt",
                    fileName = "previous.txt",
                    etag = "12345",
                    status = DownloadState.Status.DOWNLOADING,
                )
            val store =
                BrowserStore(
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")),
                        selectedTabId = "test-tab",
                        downloads = mapOf("test" to previousDownload),
                    )
                )
            val feature =
                spy(
                    DownloadsFeature(
                        applicationContext = testContext,
                        store = store,
                        mainDispatcher = testDispatcher,
                        useCases = DownloadsUseCases(store, mock()),
                        downloadFileUtils = FakeDownloadFileUtils(),
                    )
                )

            val foundDownload = feature.findDownloadWithSameEtag(download)

            assertNull(foundDownload)
        }

    @Test
    fun `WHEN file was already downloaded with same etag and different url THEN findDownloadWithSameEtag returns null`() =
        runTest(testDispatcher) {
            val download =
                DownloadState(
                    url = "https://www.mozilla.org/file.txt",
                    sessionId = "test-tab",
                    id = "test",
                    fileName = "file.txt",
                    etag = "12345",
                    directoryPath = "/downloads",
                )
            val previousDownload =
                DownloadState(
                    url = "https://www.mozilla.org/another-file.txt",
                    fileName = "previous.txt",
                    etag = "12345",
                    status = DownloadState.Status.COMPLETED,
                )
            val store =
                BrowserStore(
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")),
                        selectedTabId = "test-tab",
                        downloads = mapOf("test" to previousDownload),
                    )
                )
            val feature =
                spy(
                    DownloadsFeature(
                        applicationContext = testContext,
                        store = store,
                        mainDispatcher = testDispatcher,
                        useCases = DownloadsUseCases(store, mock()),
                        downloadFileUtils = FakeDownloadFileUtils(),
                    )
                )

            val foundDownload = feature.findDownloadWithSameEtag(download)

            assertNull(foundDownload)
        }

    @Test
    fun `WHEN file was already downloaded with same url and different etag THEN findDownloadWithSameEtag returns null`() =
        runTest(testDispatcher) {
            val download =
                DownloadState(
                    url = "https://www.mozilla.org/file.txt",
                    sessionId = "test-tab",
                    id = "test",
                    fileName = "file.txt",
                    etag = "12345",
                    directoryPath = "/downloads",
                )
            val previousDownload =
                DownloadState(
                    url = "https://www.mozilla.org/file.txt",
                    fileName = "previous.txt",
                    etag = "123456",
                    status = DownloadState.Status.COMPLETED,
                )
            val store =
                BrowserStore(
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")),
                        selectedTabId = "test-tab",
                        downloads = mapOf("test" to previousDownload),
                    )
                )
            val feature =
                spy(
                    DownloadsFeature(
                        applicationContext = testContext,
                        store = store,
                        mainDispatcher = testDispatcher,
                        useCases = DownloadsUseCases(store, mock()),
                        downloadFileUtils = FakeDownloadFileUtils(),
                    )
                )

            val foundDownload = feature.findDownloadWithSameEtag(download)

            assertNull(foundDownload)
        }

    @Test
    fun `GIVEN file was already downloaded with same url and etag but file was deleted WHEN calling findDownloadWithSameEtag THEN it returns null`() =
        runTest(testDispatcher) {
            val download =
                DownloadState(
                    url = "https://www.mozilla.org/file.txt",
                    sessionId = "test-tab",
                    id = "test",
                    fileName = "file.txt",
                    etag = "12345",
                    directoryPath = "/downloads",
                )
            val previousDownload =
                DownloadState(
                    url = "https://www.mozilla.org/file.txt",
                    fileName = "previous.txt",
                    etag = "12345",
                    status = DownloadState.Status.COMPLETED,
                )
            val store =
                BrowserStore(
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")),
                        selectedTabId = "test-tab",
                        downloads = mapOf("test" to previousDownload),
                    )
                )
            val feature =
                spy(
                    DownloadsFeature(
                        applicationContext = testContext,
                        store = store,
                        useCases = DownloadsUseCases(store, mock()),
                        fileSystemHelper = FakeFileSystemHelper(),
                        downloadFileUtils = FakeDownloadFileUtils(),
                        mainDispatcher = testDispatcher,
                    )
                )

            val foundDownload = feature.findDownloadWithSameEtag(download)

            assertNull(foundDownload)
        }
}

private fun grantPermissions() {
    grantPermission(INTERNET, WRITE_EXTERNAL_STORAGE)
}

private fun mockFragmentManager(): FragmentManager {
    val fragmentManager: FragmentManager = mock()
    doReturn(mock<FragmentTransaction>()).`when`(fragmentManager).beginTransaction()
    return fragmentManager
}
