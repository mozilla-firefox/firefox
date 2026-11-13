/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.snackbar

import android.content.Context
import android.view.View
import androidx.compose.ui.text.style.TextOverflow
import androidx.navigation.NavController
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.google.android.material.snackbar.Snackbar.LENGTH_INDEFINITE
import com.google.android.material.snackbar.Snackbar.LENGTH_LONG
import com.google.android.material.snackbar.Snackbar.LENGTH_SHORT
import io.mockk.every
import io.mockk.mockk
import io.mockk.slot
import io.mockk.verify
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.content.DownloadState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.storage.BookmarkNode
import mozilla.components.concept.storage.BookmarkNodeType
import mozilla.components.concept.sync.TabData
import mozilla.components.feature.accounts.push.SendTabUseCases
import mozilla.components.feature.accounts.push.SendTabUseCases.SendToAllUseCase
import mozilla.components.feature.accounts.push.SendTabUseCases.SendToDeviceUseCase
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.feature.tabs.TabsUseCases.UndoTabRemovalUseCase
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.utils.DownloadFileUtils
import mozilla.components.support.utils.FakeDownloadFileUtils
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.BookmarkAction
import org.mozilla.fenix.components.appstate.AppAction.ShareAction
import org.mozilla.fenix.components.appstate.AppAction.ShortcutAction
import org.mozilla.fenix.components.appstate.AppAction.SnackbarAction
import org.mozilla.fenix.components.appstate.AppAction.TranslationsAction
import org.mozilla.fenix.components.appstate.AppAction.WebCompatAction
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState.BookmarkAdded
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState.CopyLinkToClipboard
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState.CurrentTabClosed
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState.DeletingBrowserDataInProgress
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState.Dismiss
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState.DownloadFailed
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState.DownloadInProgress
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState.None
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState.ShareTabsFailed
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState.ShareToAppFailed
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState.SharedTabsSuccessfully
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState.ShortcutAdded
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState.ShowSnackbar
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState.TabAddedToGroup
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState.TabMovedToGroup
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState.TranslationInProgress
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState.UserAccountAuthenticated
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState.WebCompatReportSent
import org.mozilla.fenix.components.metrics.MetricsUtils.BookmarkAction.Source
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.tabClosedUndoMessage
import org.mozilla.fenix.home.topsites.AddShortcutEntryPoint
import org.mozilla.fenix.home.topsites.AddShortcutSource
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.tabstray.ui.AccessPoint
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.utils.getSnackbarTimeout

@RunWith(AndroidJUnit4::class)
class SnackbarBindingTest {

    private val testDispatcher = StandardTestDispatcher()
    private val appStore = AppStore()
    private val snackbarDelegate: FenixSnackbarDelegate = mockk(relaxUnitFun = true)
    private val navController: NavController = mockk(relaxUnitFun = true)
    private val tabsUseCases: TabsUseCases = mockk()
    private var settings: Settings = mockk()

    @Before
    fun setup() =
        runTest(testDispatcher) {
            settings = mockk()
            every { settings.accessibilityServicesEnabled } returns false
        }

    @Test
    fun `GIVEN translation is in progress for the current selected session WHEN snackbar state is updated to translation in progress THEN display the snackbar`() =
        runTest(testDispatcher) {
            val sessionId = "sessionId"
            val tab = createTab(url = "https://www.mozilla.org", id = sessionId)
            val browserStore =
                BrowserStore(
                    BrowserState(
                        tabs = listOf(tab),
                        selectedTabId = sessionId,
                    )
                )
            val binding = buildSnackbarBinding(browserStore = browserStore, scope = this)
            binding.start()

            appStore.dispatch(TranslationsAction.TranslationStarted(sessionId = sessionId))
            waitForStoreToSettle()

            verify {
                snackbarDelegate.show(
                    text = R.string.translation_in_progress_snackbar,
                    duration = LENGTH_INDEFINITE,
                    isError = false,
                )
            }

            assertEquals(None(TranslationInProgress(sessionId)), appStore.state.snackbarState)
        }

    @Test
    fun `GIVEN translation is in progress for a different session WHEN snackbar state is updated to translation in progress THEN do not display the snackbar`() =
        runTest(testDispatcher) {
            val tab1 = createTab(url = "https://www.mozilla.org", id = "1")
            val tab2 = createTab(url = "https://www.mozilla.org", id = "2")
            val browserStore =
                BrowserStore(
                    BrowserState(
                        tabs = listOf(tab1, tab2),
                        selectedTabId = tab1.id,
                    )
                )
            val binding = buildSnackbarBinding(browserStore = browserStore, scope = this)
            binding.start()

            appStore.dispatch(TranslationsAction.TranslationStarted(sessionId = tab2.id))
            waitForStoreToSettle()

            verify(exactly = 0) {
                snackbarDelegate.show(
                    text = R.string.translation_in_progress_snackbar,
                    duration = LENGTH_LONG,
                    isError = false,
                )
            }
        }

    @Test
    fun `WHEN the snackbar state is updated to dismiss THEN dismiss the snackbar`() =
        runTest(testDispatcher) {
            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            appStore.dispatch(SnackbarAction.SnackbarDismissed)
            waitForStoreToSettle()

            assertEquals(None(Dismiss(None())), appStore.state.snackbarState)
            verify { snackbarDelegate.dismiss() }
        }

    @Test
    fun `WHEN show snackbar action is dispatched with a title THEN display the snackbar with that title`() =
        runTest(testDispatcher) {
            val customTitle = "Custom Title"
            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            appStore.dispatch(SnackbarAction.ShowSnackbar(customTitle))
            waitForStoreToSettle()

            verify {
                snackbarDelegate.show(
                    text = customTitle,
                    duration = LENGTH_SHORT,
                    isError = false,
                )
            }

            assertEquals(None(ShowSnackbar(customTitle)), appStore.state.snackbarState)
        }

    @Test
    fun `GIVEN bookmark's parent is a root node WHEN the bookmark added state is observed THEN display friendly title`() =
        runTest(testDispatcher) {
            val parent = buildParentBookmarkNode(guid = BookmarkRoot.Mobile.id, title = "mobile")
            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            appStore.dispatch(
                BookmarkAction.BookmarkAdded(
                    guidToEdit = "1",
                    parentNode = parent,
                    source = Source.TEST,
                )
            )
            waitForStoreToSettle()

            assertEquals(None(BookmarkAdded("1", parent)), appStore.state.snackbarState)

            val outputMessage = testContext.getString(R.string.bookmark_saved_in_folder_snackbar, "Bookmarks")
            verify {
                snackbarDelegate.show(
                    text = eq(outputMessage),
                    subText = isNull(),
                    subTextOverflow = isNull(),
                    duration = eq(LENGTH_LONG),
                    isError = eq(false),
                    action = eq("EDIT"),
                    withDismissAction = eq(false),
                    listener = any(),
                )
            }
        }

    @Test
    fun `GIVEN bookmark's parent is not a root node but has a root node title WHEN the bookmark added state is observed THEN display custom title`() =
        runTest(testDispatcher) {
            val parent = buildParentBookmarkNode(title = "mobile", guid = "not a root")
            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            appStore.dispatch(
                BookmarkAction.BookmarkAdded(
                    guidToEdit = "1",
                    parentNode = parent,
                    source = Source.TEST,
                )
            )

            waitForStoreToSettle()

            assertEquals(None(BookmarkAdded("1", parent)), appStore.state.snackbarState)

            val outputMessage = testContext.getString(R.string.bookmark_saved_in_folder_snackbar, "mobile")
            verify {
                snackbarDelegate.show(
                    text = eq(outputMessage),
                    subText = isNull(),
                    subTextOverflow = isNull(),
                    duration = eq(LENGTH_LONG),
                    isError = eq(false),
                    action = eq(testContext.getString(R.string.edit_bookmark_snackbar_action)),
                    withDismissAction = eq(false),
                    listener = any(),
                )
            }
        }

    @Test
    fun `GIVEN no bookmark is added WHEN the bookmark added state is observed THEN display the error snackbar`() =
        runTest(testDispatcher) {
            val parent = buildParentBookmarkNode()
            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            appStore.dispatch(
                BookmarkAction.BookmarkAdded(guidToEdit = null, parentNode = parent, source = Source.TEST)
            )

            waitForStoreToSettle()

            assertEquals(None(BookmarkAdded(null, parent)), appStore.state.snackbarState)
            verify {
                snackbarDelegate.show(
                    text = R.string.bookmark_invalid_url_error,
                    duration = LENGTH_LONG,
                )
            }
        }

    @Test
    fun `GIVEN there is no parent folder for an added bookmark WHEN the bookmark added state is observed THEN display the error snackbar`() =
        runTest(testDispatcher) {
            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            appStore.dispatch(
                BookmarkAction.BookmarkAdded(guidToEdit = "guid", parentNode = null, source = Source.TEST)
            )
            waitForStoreToSettle()

            assertEquals(None(BookmarkAdded("guid", null)), appStore.state.snackbarState)
            verify {
                snackbarDelegate.show(
                    text = R.string.bookmark_invalid_url_error,
                    duration = LENGTH_LONG,
                    isError = false,
                )
            }
        }

    @Test
    fun `WHEN the shortcut added state action is dispatched THEN display the appropriate snackbar`() =
        runTest(testDispatcher) {
            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            appStore.dispatch(
                ShortcutAction.ShortcutAdded(
                    url = "https://www.mozilla.org",
                    source = AddShortcutSource.MANUAL,
                    entryPoint = AddShortcutEntryPoint.PAGE_MENU,
                )
            )
            waitForStoreToSettle()

            assertEquals(None(ShortcutAdded), appStore.state.snackbarState)
            verify {
                snackbarDelegate.show(
                    text = R.string.snackbar_added_to_shortcuts,
                    duration = LENGTH_LONG,
                    isError = false,
                )
            }
        }

    @Test
    fun `WHEN the delete and quit selected state action is dispatched THEN display the appropriate snackbar`() =
        runTest(testDispatcher) {
            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            appStore.dispatch(AppAction.DeleteAndQuitStarted)
            waitForStoreToSettle()

            assertEquals(None(DeletingBrowserDataInProgress), appStore.state.snackbarState)
            verify {
                snackbarDelegate.show(
                    text = R.string.deleting_browsing_data_in_progress,
                    duration = LENGTH_INDEFINITE,
                    isError = false,
                )
            }
        }

    @Test
    fun `WHEN the user has successfully signed in THEN display the appropriate snackbar`() =
        runTest(testDispatcher) {
            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            appStore.dispatch(AppAction.UserAccountAuthenticated)

            waitForStoreToSettle()

            assertEquals(None(UserAccountAuthenticated), appStore.state.snackbarState)
            verify {
                snackbarDelegate.show(
                    text = R.string.sync_syncing_in_progress,
                    duration = LENGTH_SHORT,
                )
            }

            assertEquals(None(UserAccountAuthenticated), appStore.state.snackbarState)
        }

    @Test
    fun `WHEN share to app failed THEN display a snackbar`() =
        runTest(testDispatcher) {
            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            appStore.dispatch(ShareAction.ShareToAppFailed)
            waitForStoreToSettle()

            verify {
                snackbarDelegate.show(
                    text = R.string.share_error_snackbar,
                    duration = LENGTH_LONG,
                    isError = false,
                )
            }

            assertEquals(None(ShareToAppFailed), appStore.state.snackbarState)
        }

    @Test
    fun `WHEN sharing a tab was successful THEN display an appropriate snackbar`() =
        runTest(testDispatcher) {
            val destinations = listOf("a")
            val sharedTabs = listOf(mockk<TabData>())
            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            appStore.dispatch(ShareAction.SharedTabsSuccessfully(destinations, sharedTabs))
            waitForStoreToSettle()

            verify {
                snackbarDelegate.show(
                    text = R.string.sync_sent_tab_snackbar_2,
                    duration = LENGTH_SHORT,
                    isError = false,
                )
            }

            assertEquals(None(SharedTabsSuccessfully(destinations, sharedTabs)), appStore.state.snackbarState)
        }

    @Test
    fun `WHEN sharing multiple tabs was successful THEN display an appropriate snackbar`() =
        runTest(testDispatcher) {
            val destinations = listOf("a")
            val sharedTabs = listOf(mockk<TabData>(), mockk<TabData>())
            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            appStore.dispatch(ShareAction.SharedTabsSuccessfully(destinations, sharedTabs))
            waitForStoreToSettle()

            verify {
                snackbarDelegate.show(
                    text = R.string.sync_sent_tabs_snackbar_2,
                    duration = LENGTH_SHORT,
                    isError = false,
                )
            }

            assertEquals(None(SharedTabsSuccessfully(destinations, sharedTabs)), appStore.state.snackbarState)
        }

    @Test
    fun `WHEN sharing tabs failed THEN show a snackbar`() =
        runTest(testDispatcher) {
            val destinations = listOf("a")
            val sharedTabs = listOf(mockk<TabData>())
            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            appStore.dispatch(ShareAction.ShareTabsFailed(destinations, sharedTabs))
            waitForStoreToSettle()

            verify {
                snackbarDelegate.show(
                    text = eq(R.string.sync_sent_tab_error_snackbar),
                    duration = eq(LENGTH_LONG),
                    isError = eq(true),
                    action = eq(R.string.sync_sent_tab_error_snackbar_action),
                    withDismissAction = eq(false),
                    listener = any(),
                )
            }

            assertEquals(None(ShareTabsFailed(destinations, sharedTabs)), appStore.state.snackbarState)
        }

    @Test
    fun `GIVEN sharing tabs to another device failed and user chose to retry WHEN this succeeds THEN show a snackbar`() =
        runTest(testDispatcher) {
            val destinations = listOf("a")
            val sharedTabs = listOf(mockk<TabData>())
            val retryActionCaptor = slot<((v: View) -> Unit)>()
            val sendTabUseCases: SendTabUseCases = mockk()
            val sendToDeviceUseCase: SendToDeviceUseCase = mockk()
            every { sendTabUseCases.sendToDeviceAsync } returns sendToDeviceUseCase
            val retryResult = CompletableDeferred(true)
            every { sendToDeviceUseCase.invoke(any<String>(), any<List<TabData>>()) } returns retryResult
            val binding = buildSnackbarBinding(sendTabUseCases = sendTabUseCases, scope = this)
            binding.start()

            appStore.dispatch(ShareAction.ShareTabsFailed(destinations, sharedTabs))
            waitForStoreToSettle()

            verify {
                snackbarDelegate.show(
                    text = eq(R.string.sync_sent_tab_error_snackbar),
                    duration = eq(LENGTH_LONG),
                    isError = eq(true),
                    action = eq(R.string.sync_sent_tab_error_snackbar_action),
                    withDismissAction = eq(false),
                    listener = capture(retryActionCaptor),
                )
            }

            retryActionCaptor.captured.invoke(mockk())
            waitForStoreToSettle()

            verify {
                snackbarDelegate.show(
                    text = R.string.sync_sent_tab_snackbar_2,
                    duration = LENGTH_SHORT,
                    isError = false,
                )
            }

            assertEquals(None(SharedTabsSuccessfully(destinations, sharedTabs)), appStore.state.snackbarState)
        }

    @Test
    fun `GIVEN sharing tabs to other devices failed and user chose to retry WHEN this fails again THEN show a snackbar`() =
        runTest(testDispatcher) {
            val destinations = listOf("a", "b")
            val sharedTabs = listOf(mockk<TabData>())
            val retryActionCaptor = slot<((v: View) -> Unit)>()
            val sendTabUseCases: SendTabUseCases = mockk()
            val sendToAllDevicesUseCase: SendToAllUseCase = mockk()
            every { sendTabUseCases.sendToAllAsync } returns sendToAllDevicesUseCase
            val retryResult = CompletableDeferred(false)
            every { sendToAllDevicesUseCase.invoke(any<List<TabData>>()) } returns retryResult
            val binding = buildSnackbarBinding(sendTabUseCases = sendTabUseCases, scope = this)
            binding.start()

            appStore.dispatch(ShareAction.ShareTabsFailed(destinations, sharedTabs))
            waitForStoreToSettle()

            verify {
                snackbarDelegate.show(
                    text = eq(R.string.sync_sent_tab_error_snackbar),
                    duration = eq(LENGTH_LONG),
                    isError = eq(true),
                    action = eq(R.string.sync_sent_tab_error_snackbar_action),
                    withDismissAction = eq(false),
                    listener = capture(retryActionCaptor),
                )
            }

            retryActionCaptor.captured.invoke(mockk())
            waitForStoreToSettle()

            verify(exactly = 2) {
                snackbarDelegate.show(
                    text = eq(R.string.sync_sent_tab_error_snackbar),
                    duration = eq(LENGTH_LONG),
                    isError = eq(true),
                    action = eq(R.string.sync_sent_tab_error_snackbar_action),
                    withDismissAction = eq(false),
                    listener = any(),
                )
            }

            assertEquals(None(ShareTabsFailed(destinations, sharedTabs)), appStore.state.snackbarState)
        }

    @Test
    fun `WHEN a link is copied to clipboard THEN display a snackbar`() =
        runTest(testDispatcher) {
            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            appStore.dispatch(ShareAction.CopyLinkToClipboard)
            waitForStoreToSettle()

            verify {
                snackbarDelegate.show(
                    text = R.string.toast_copy_link_to_clipboard,
                    duration = LENGTH_SHORT,
                )
            }

            assertEquals(None(CopyLinkToClipboard), appStore.state.snackbarState)
        }

    @Test
    fun `WHEN the current tab is closed THEN display a snackbar`() =
        runTest(testDispatcher) {
            val snackbarAction = slot<((v: View) -> Unit)>()
            val undoUsecase: UndoTabRemovalUseCase = mockk(relaxUnitFun = true)
            every { tabsUseCases.undo } returns undoUsecase
            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            appStore.dispatch(AppAction.CurrentTabClosed(isPrivate = false))
            waitForStoreToSettle()

            verify {
                snackbarDelegate.show(
                    text = eq(testContext.tabClosedUndoMessage(false)),
                    subText = isNull(),
                    subTextOverflow = isNull(),
                    duration = eq(LENGTH_LONG),
                    isError = eq(false),
                    action = eq(testContext.getString(R.string.snackbar_deleted_undo)),
                    withDismissAction = eq(false),
                    listener = capture(snackbarAction),
                )
            }
            snackbarAction.captured.invoke(mockk())
            verify { undoUsecase.invoke() }

            assertEquals(None(CurrentTabClosed(false)), appStore.state.snackbarState)
        }

    @Test
    fun `WHEN download is failed THEN display a snackbar`() =
        runTest(testDispatcher) {
            val snackbarAction = slot<((v: View) -> Unit)>()
            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            appStore.dispatch(AppAction.DownloadAction.DownloadFailed(fileName = "fileName"))
            waitForStoreToSettle()

            verify {
                snackbarDelegate.show(
                    text = eq(testContext.getString(R.string.download_item_status_failed)),
                    subText = eq("fileName"),
                    subTextOverflow = eq(TextOverflow.MiddleEllipsis),
                    duration = eq(LENGTH_INDEFINITE),
                    isError = eq(false),
                    action = eq(testContext.getString(R.string.download_failed_snackbar_action_details)),
                    withDismissAction = eq(true),
                    listener = capture(snackbarAction),
                )
            }
            snackbarAction.captured.invoke(mockk())

            verify {
                navController.navigate(BrowserFragmentDirections.actionGlobalDownloadsFragment())
            }

            assertEquals(None(DownloadFailed("fileName")), appStore.state.snackbarState)
        }

    @Test
    fun `WHEN download is completed THEN display a snackbar`() =
        runTest(testDispatcher) {
            val snackbarAction = slot<((v: View) -> Unit)>()
            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            val downloadState =
                DownloadState(
                    id = "1",
                    url = "url",
                    fileName = "fileName",
                    contentType = "application/zip",
                    contentLength = 5242880,
                    status = DownloadState.Status.DOWNLOADING,
                    directoryPath = "downloads",
                    private = true,
                    createdTime = 33,
                    etag = "etag",
                )

            appStore.dispatch(AppAction.DownloadAction.DownloadCompleted(downloadState))
            waitForStoreToSettle()

            verify {
                snackbarDelegate.show(
                    text = eq(testContext.getString(R.string.download_completed_snackbar)),
                    subText = eq("fileName"),
                    subTextOverflow = eq(TextOverflow.MiddleEllipsis),
                    duration = eq(testContext.components.settings.getSnackbarTimeout(hasAction = true).value.toInt()),
                    isError = eq(false),
                    action = eq(testContext.getString(R.string.download_completed_snackbar_action_open)),
                    withDismissAction = eq(false),
                    listener = capture(snackbarAction),
                )
            }
        }

    @Test
    fun `WHEN download is completed and user opens file THEN the downloads menu notification is removed`() =
        runTest(testDispatcher) {
            val snackbarAction = slot<((v: View) -> Unit)>()
            appStore.dispatch(AppAction.MenuNotification.AddMenuNotification(SupportedMenuNotifications.Downloads))

            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            val downloadState =
                DownloadState(
                    id = "1",
                    url = "url",
                    fileName = "fileName",
                    contentType = "application/zip",
                    contentLength = 5242880,
                    status = DownloadState.Status.DOWNLOADING,
                    directoryPath = "downloads",
                    private = true,
                    createdTime = 33,
                    etag = "etag",
                )

            appStore.dispatch(AppAction.DownloadAction.DownloadCompleted(downloadState))
            waitForStoreToSettle()

            verify {
                snackbarDelegate.show(
                    any(),
                    any(),
                    any(),
                    any(),
                    any(),
                    any(),
                    any(),
                    capture(snackbarAction),
                )
            }
            snackbarAction.captured.invoke(mockk())
            waitForStoreToSettle()

            assertFalse(appStore.state.supportedMenuNotifications.contains(SupportedMenuNotifications.Downloads))
        }

    @Test
    fun `WHEN download file can't be open THEN display a snackbar`() =
        runTest(testDispatcher) {
            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            val downloadState =
                DownloadState(
                    id = "1",
                    url = "url",
                    fileName = "fileName",
                    contentType = "application/zip",
                    contentLength = 5242880,
                    status = DownloadState.Status.DOWNLOADING,
                    directoryPath = "downloads",
                    private = true,
                    createdTime = 33,
                    etag = "etag",
                )

            appStore.dispatch(AppAction.DownloadAction.CannotOpenFile(downloadState))
            waitForStoreToSettle()

            verify {
                snackbarDelegate.show(
                    text = "No app found to open  files",
                    duration = testContext.components.settings.getSnackbarTimeout(hasAction = false).value.toInt(),
                    isError = false,
                )
            }
        }

    @Test
    fun `WHEN download file is in progress THEN display a snackbar`() =
        runTest(testDispatcher) {
            val snackbarAction = slot<((v: View) -> Unit)>()
            val binding =
                buildSnackbarBinding(
                    browserStore =
                        BrowserStore(
                            BrowserState(
                                tabs = listOf(createTab("https://www.firefox.com", id = "id")),
                                selectedTabId = "id",
                            )
                        ),
                    scope = this,
                )
            binding.start()

            appStore.dispatch(AppAction.DownloadAction.DownloadInProgress(downloadId = "id"))
            waitForStoreToSettle()

            verify {
                snackbarDelegate.show(
                    text = eq(testContext.getString(R.string.download_in_progress_snackbar)),
                    subText = isNull(),
                    subTextOverflow = isNull(),
                    duration = eq(testContext.components.settings.getSnackbarTimeout(hasAction = true).value.toInt()),
                    isError = eq(false),
                    action = eq(testContext.getString(R.string.download_in_progress_snackbar_action_details)),
                    withDismissAction = eq(false),
                    listener = capture(snackbarAction),
                )
            }
            snackbarAction.captured.invoke(mockk())

            verify { navController.navigate(BrowserFragmentDirections.actionGlobalDownloadsFragment()) }

            assertEquals(None(DownloadInProgress("id")), appStore.state.snackbarState)
        }

    @Test
    fun `WHEN a webcompat report is successfully sent THEN show a snackbar`() =
        runTest(testDispatcher) {
            val snackbarAction = slot<((v: View) -> Unit)>()
            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            appStore.dispatch(WebCompatAction.WebCompatReportSent)
            waitForStoreToSettle()

            verify {
                snackbarDelegate.show(
                    text = eq(testContext.getString(R.string.webcompat_reporter_success_snackbar_text_2)),
                    subText = isNull(),
                    subTextOverflow = isNull(),
                    duration = eq(testContext.components.settings.getSnackbarTimeout().value.toInt()),
                    isError = eq(false),
                    action = isNull(),
                    withDismissAction = eq(false),
                    listener = capture(snackbarAction),
                )
            }

            assertEquals(None(WebCompatReportSent), appStore.state.snackbarState)

            verify(exactly = 0) { snackbarDelegate.dismiss() }
            snackbarAction.captured.invoke(mockk())
            verify { snackbarDelegate.dismiss() }
        }

    @Test
    fun `WHEN a tab is added to a group THEN display a snackbar that opens the group in the tabs tray`() =
        runTest(testDispatcher) {
            val viewAction = slot<((v: View) -> Unit)>()
            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            appStore.dispatch(AppAction.TabAddedToGroup(groupTitle = "Work", groupId = "group-1"))
            waitForStoreToSettle()

            verify {
                snackbarDelegate.show(
                    text = eq(testContext.getString(R.string.snackbar_tab_added_to_group, "Work")),
                    subText = isNull(),
                    subTextOverflow = isNull(),
                    duration = eq(testContext.components.settings.getSnackbarTimeout(hasAction = true).value.toInt()),
                    isError = eq(false),
                    action = eq(testContext.getString(R.string.snackbar_action_view)),
                    withDismissAction = eq(false),
                    listener = capture(viewAction),
                )
            }
            assertEquals(None(TabAddedToGroup(groupTitle = "Work", groupId = "group-1")), appStore.state.snackbarState)

            viewAction.captured.invoke(mockk())

            verify { navController.navigate(tabsTrayWithGroupDirections(groupId = "group-1")) }
        }

    @Test
    fun `WHEN a tab is moved to a group THEN display a snackbar that opens the group in the tabs tray`() =
        runTest(testDispatcher) {
            val viewAction = slot<((v: View) -> Unit)>()
            val binding = buildSnackbarBinding(scope = this)
            binding.start()

            appStore.dispatch(AppAction.TabMovedToGroup(groupTitle = "Work", groupId = "group-1"))
            waitForStoreToSettle()

            verify {
                snackbarDelegate.show(
                    text = eq(testContext.getString(R.string.snackbar_tab_moved_to_group, "Work")),
                    subText = isNull(),
                    subTextOverflow = isNull(),
                    duration = eq(testContext.components.settings.getSnackbarTimeout(hasAction = true).value.toInt()),
                    isError = eq(false),
                    action = eq(testContext.getString(R.string.snackbar_action_view)),
                    withDismissAction = eq(false),
                    listener = capture(viewAction),
                )
            }
            assertEquals(None(TabMovedToGroup(groupTitle = "Work", groupId = "group-1")), appStore.state.snackbarState)

            viewAction.captured.invoke(mockk())

            verify { navController.navigate(tabsTrayWithGroupDirections(groupId = "group-1")) }
        }

    private fun tabsTrayWithGroupDirections(groupId: String) =
        BrowserFragmentDirections.actionGlobalTabManagementFragment(
            enterMultiselect = false,
            accessPoint = AccessPoint.None,
            page = Page.NormalTabs,
            groupId = groupId,
        )

    private fun buildSnackbarBinding(
        context: Context = testContext,
        browserStore: BrowserStore = BrowserStore(),
        appStore: AppStore = this.appStore,
        snackbarDelegate: FenixSnackbarDelegate = this.snackbarDelegate,
        navController: NavController = this.navController,
        tabsUseCases: TabsUseCases = this.tabsUseCases,
        sendTabUseCases: SendTabUseCases? = null,
        customTabSessionId: String? = null,
        downloadFileUtils: DownloadFileUtils = FakeDownloadFileUtils(),
        scope: CoroutineScope,
    ) =
        SnackbarBinding(
            context = context,
            browserStore = browserStore,
            appStore = appStore,
            snackbarDelegate = snackbarDelegate,
            navController = navController,
            tabsUseCases = tabsUseCases,
            sendTabUseCases = sendTabUseCases,
            customTabSessionId = customTabSessionId,
            downloadFileUtils = downloadFileUtils,
            ioDispatcher = testDispatcher,
            mainDispatcher = testDispatcher,
            applicationScope = scope,
        )

    private fun waitForStoreToSettle() =
        runTest(testDispatcher) {
            // Run the enqueued tasks
            testDispatcher.scheduler.advanceUntilIdle()
        }

    private fun buildParentBookmarkNode(
        guid: String = "guid",
        title: String = "title",
    ) =
        BookmarkNode(
            type = BookmarkNodeType.FOLDER,
            guid = guid,
            parentGuid = "parentGuid",
            position = 0U,
            title = title,
            url = null,
            dateAdded = 0,
            lastModified = 0,
            children = listOf(),
        )
}
