/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.snackbar

import android.content.Context
import androidx.compose.ui.text.style.TextOverflow
import androidx.navigation.NavController
import com.google.android.material.snackbar.BaseTransientBottomBar.LENGTH_INDEFINITE
import com.google.android.material.snackbar.Snackbar
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.browser.state.selector.findCustomTabOrSelectedTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.accounts.push.SendTabUseCases
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.lib.state.helpers.AbstractBinding
import mozilla.components.support.utils.DefaultDownloadFileUtils
import mozilla.components.support.utils.DownloadFileUtils
import mozilla.components.ui.widgets.SnackbarDelegate
import org.mozilla.fenix.GleanMetrics.SentFromFirefox
import org.mozilla.fenix.R
import org.mozilla.fenix.bookmarks.BookmarksGlobalResultReport
import org.mozilla.fenix.bookmarks.friendlyRootTitle
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.ShareAction
import org.mozilla.fenix.components.appstate.AppAction.SnackbarAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState
import org.mozilla.fenix.downloads.getCannotOpenFileErrorMessage
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.navigateWithBreadcrumb
import org.mozilla.fenix.ext.tabClosedUndoMessage
import org.mozilla.fenix.ipprotection.ui.IPProtectionSnackbarBinding
import org.mozilla.fenix.settings.downloads.DownloadLocationManager
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.tabstray.ui.AccessPoint
import org.mozilla.fenix.utils.getSnackbarTimeout

/**
 * A binding for observing the [SnackbarState] in the [AppStore] and displaying the snackbar.
 *
 * @param context The Android [Context] used for system interactions and accessing resources.
 * @param browserStore The [BrowserStore] used to get the current session.
 * @param appStore The [AppStore] used to observe the [SnackbarState].
 * @param snackbarDelegate The [SnackbarDelegate] used to display a snackbar.
 * @param navController [NavController] used for navigation.
 * @param tabsUseCases [TabsUseCases] used to manage tabs.
 * @param sendTabUseCases [SendTabUseCases] used to send tabs to other devices.
 * @param customTabSessionId Optional custom tab session ID if navigating from a custom tab or null if the selected
 *   session should be used.
 * @param applicationScope [CoroutineScope] tied to the application lifetime, used for retry operations that must
 *   outlive the current UI component (e.g. retrying a failed send-tab-to-device action).
 * @param viewHasFocus Whether the host view is currently focused. Used to determine if the binding should consume the
 *   snackbar in case there are multiple bindings active (e.g., menu is shown on top of the home fragment, and both host
 *   snackbar bindings).
 * @param downloadFileUtils [DownloadFileUtils] used for file-related operations in download snackbars.
 * @param ioDispatcher The [CoroutineDispatcher] used for background operations executed when the user starts a snackbar
 *   action.
 * @param mainDispatcher The [CoroutineDispatcher] on which the state observation and updates will occur. Defaults to
 *   [Dispatchers.Main].
 */
@Suppress("LongParameterList")
class SnackbarBinding(
    private val context: Context,
    private val browserStore: BrowserStore,
    private val appStore: AppStore,
    private val snackbarDelegate: FenixSnackbarDelegate,
    private val navController: NavController,
    private val tabsUseCases: TabsUseCases,
    private val sendTabUseCases: SendTabUseCases?,
    private val customTabSessionId: String?,
    private val applicationScope: CoroutineScope,
    private val viewHasFocus: () -> Boolean = { true },
    private val downloadFileUtils: DownloadFileUtils =
        DefaultDownloadFileUtils(
            context = context,
            downloadLocation = {
                DownloadLocationManager(context.components.settings, context.contentResolver).defaultLocation
            },
        ),
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : AbstractBinding<AppState>(appStore, mainDispatcher) {

    private val currentSession
        get() = browserStore.state.findCustomTabOrSelectedTab(customTabSessionId)

    @Suppress("LongMethod", "CognitiveComplexMethod", "CyclomaticComplexMethod")
    override suspend fun onState(flow: Flow<AppState>) {
        flow
            .map { state -> state.snackbarState }
            .distinctUntilChanged()
            .collect { state ->
                when (state) {
                    is SnackbarState.ShowSnackbar -> {
                        snackbarDelegate.show(
                            text = state.title,
                            duration = state.duration,
                        )

                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    is SnackbarState.BookmarkAdded -> {
                        showBookmarkAddedSnackbarFor(state)
                    }

                    is SnackbarState.BookmarkOperationResultReported -> {
                        showBookmarkResultSnackbar(state)
                    }

                    is SnackbarState.ReportSent -> {
                        snackbarDelegate.show(
                            text = R.string.crash_reporting_snack_bar_message,
                            duration = Snackbar.LENGTH_SHORT,
                        )

                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    is SnackbarState.BookmarkDeleted -> {
                        snackbarDelegate.show(
                            text = context.getString(R.string.bookmark_deletion_snackbar_message, state.title),
                            duration = Snackbar.LENGTH_LONG,
                        )

                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    is SnackbarState.ShortcutAdded -> {
                        snackbarDelegate.show(
                            text = R.string.snackbar_added_to_shortcuts,
                            duration = Snackbar.LENGTH_LONG,
                        )
                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    is SnackbarState.DeletingBrowserDataInProgress -> {
                        snackbarDelegate.show(
                            text = R.string.deleting_browsing_data_in_progress,
                            duration = Snackbar.LENGTH_INDEFINITE,
                        )

                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    is SnackbarState.Dismiss -> {
                        snackbarDelegate.dismiss()
                        appStore.dispatch(SnackbarAction.Reset)
                    }

                    is SnackbarState.TranslationInProgress -> {
                        if (currentSession?.id != state.sessionId) {
                            return@collect
                        }

                        snackbarDelegate.show(
                            text = R.string.translation_in_progress_snackbar,
                            duration = Snackbar.LENGTH_INDEFINITE,
                        )

                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    is SnackbarState.UserAccountAuthenticated -> {
                        snackbarDelegate.show(
                            text = R.string.sync_syncing_in_progress,
                            duration = Snackbar.LENGTH_SHORT,
                        )

                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    is SnackbarState.ShareToAppFailed -> {
                        snackbarDelegate.show(
                            text = R.string.share_error_snackbar,
                            duration = Snackbar.LENGTH_LONG,
                        )

                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    is SnackbarState.ShareToWhatsApp -> {
                        snackbarDelegate.show(
                            text = R.string.link_shared_snackbar_message,
                            duration = Snackbar.LENGTH_LONG,
                            action = R.string.link_shared_snackbar_action,
                        ) {
                            SentFromFirefox.snackbarClicked.record()
                            // Navigating twice ensures the correct behavior when opening the link
                            // sharing settings screen. The first navigation scrolls to the link
                            // sharing section in the settings screen. The second navigation opens
                            // the dedicated link sharing settings screen.
                            navController.navigate(
                                BrowserFragmentDirections.actionBrowserFragmentToSettingsFragment(
                                    preferenceToScrollTo = context.getString(R.string.pref_key_link_sharing)
                                )
                            )
                            navController.navigate(BrowserFragmentDirections.actionGlobalLinkSharingFragment())
                        }

                        context.components.settings.linkSharingSettingsSnackbarShown = true
                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    is SnackbarState.SharedTabsSuccessfully -> {
                        snackbarDelegate.show(
                            text =
                                when (state.tabs.size) {
                                    1 -> R.string.sync_sent_tab_snackbar_2
                                    else -> R.string.sync_sent_tabs_snackbar_2
                                },
                            duration = Snackbar.LENGTH_SHORT,
                        )

                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    is SnackbarState.ShareTabsFailed -> {
                        snackbarDelegate.show(
                            text = R.string.sync_sent_tab_error_snackbar,
                            duration = Snackbar.LENGTH_LONG,
                            isError = true,
                            action = R.string.sync_sent_tab_error_snackbar_action,
                        ) {
                            sendTabUseCases ?: return@show

                            applicationScope.launch(ioDispatcher) {
                                val operation =
                                    when (state.destination.size) {
                                        1 ->
                                            sendTabUseCases.sendToDeviceAsync(
                                                deviceId = state.destination[0],
                                                tabs = state.tabs,
                                            )
                                        else -> sendTabUseCases.sendToAllAsync(tabs = state.tabs)
                                    }
                                when (operation.await()) {
                                    true ->
                                        appStore.dispatch(
                                            ShareAction.SharedTabsSuccessfully(
                                                destination = state.destination,
                                                tabs = state.tabs,
                                            )
                                        )

                                    false ->
                                        appStore.dispatch(
                                            ShareAction.ShareTabsFailed(
                                                destination = state.destination,
                                                tabs = state.tabs,
                                            )
                                        )
                                }
                            }
                        }

                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    SnackbarState.CopyLinkToClipboard -> {
                        snackbarDelegate.show(
                            text = R.string.toast_copy_link_to_clipboard,
                            duration = Snackbar.LENGTH_SHORT,
                        )

                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    SnackbarState.WebCompatReportSent -> {
                        snackbarDelegate.show(
                            text = context.getString(R.string.webcompat_reporter_success_snackbar_text_2),
                            duration = context.components.settings.getSnackbarTimeout().value.toInt(),
                            listener = { snackbarDelegate.dismiss() },
                        )

                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    is SnackbarState.CurrentTabClosed -> {
                        snackbarDelegate.show(
                            text = context.tabClosedUndoMessage(state.isPrivate),
                            action = context.getString(R.string.snackbar_deleted_undo),
                        ) {
                            tabsUseCases.undo()
                        }

                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    is SnackbarState.DownloadInProgress -> {
                        snackbarDelegate.show(
                            text = context.getString(R.string.download_in_progress_snackbar),
                            duration = context.components.settings.getSnackbarTimeout(hasAction = true).value.toInt(),
                            action = context.getString(R.string.download_in_progress_snackbar_action_details),
                        ) {
                            navController.navigate(BrowserFragmentDirections.actionGlobalDownloadsFragment())
                        }

                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    is SnackbarState.DownloadFailed -> {
                        snackbarDelegate.show(
                            text = context.getString(R.string.download_item_status_failed),
                            subText = state.fileName,
                            subTextOverflow = TextOverflow.MiddleEllipsis,
                            duration = LENGTH_INDEFINITE,
                            action = context.getString(R.string.download_failed_snackbar_action_details),
                            withDismissAction = true,
                        ) {
                            navController.navigate(BrowserFragmentDirections.actionGlobalDownloadsFragment())
                        }
                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    is SnackbarState.DownloadCompleted -> {
                        snackbarDelegate.show(
                            text = context.getString(R.string.download_completed_snackbar),
                            subText = state.downloadState.fileName,
                            subTextOverflow = TextOverflow.MiddleEllipsis,
                            duration = context.components.settings.getSnackbarTimeout(hasAction = true).value.toInt(),
                            action = context.getString(R.string.download_completed_snackbar_action_open),
                        ) {
                            val fileWasOpened =
                                downloadFileUtils.openFile(
                                    fileName = state.downloadState.fileName,
                                    directoryPath = state.downloadState.directoryPath,
                                    contentType = state.downloadState.contentType,
                                )

                            if (fileWasOpened) {
                                appStore.dispatch(
                                    AppAction.MenuNotification.RemoveMenuNotification(
                                        SupportedMenuNotifications.Downloads
                                    )
                                )
                            } else {
                                appStore.dispatch(AppAction.DownloadAction.CannotOpenFile(state.downloadState))
                            }
                        }
                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    is SnackbarState.CannotOpenFileError -> {
                        snackbarDelegate.show(
                            text =
                                getCannotOpenFileErrorMessage(
                                    context,
                                    state.downloadState.filePath,
                                ),
                            duration = context.components.settings.getSnackbarTimeout(hasAction = false).value.toInt(),
                        )
                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    is SnackbarState.TabAddedToGroup -> {
                        snackbarDelegate.show(
                            text = context.getString(R.string.snackbar_tab_added_to_group, state.groupTitle),
                            duration = context.components.settings.getSnackbarTimeout(hasAction = true).value.toInt(),
                            action = context.getString(R.string.snackbar_action_view),
                        ) {
                            navigateToTabsTrayWithGroup(state.groupId)
                        }
                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    is SnackbarState.TabMovedToGroup -> {
                        snackbarDelegate.show(
                            text = context.getString(R.string.snackbar_tab_moved_to_group, state.groupTitle),
                            duration = context.components.settings.getSnackbarTimeout(hasAction = true).value.toInt(),
                            action = context.getString(R.string.snackbar_action_view),
                        ) {
                            navigateToTabsTrayWithGroup(state.groupId)
                        }
                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    is SnackbarState.URLCopiedToClipboard -> {
                        snackbarDelegate.show(
                            text = context.getString(R.string.browser_toolbar_url_copied_to_clipboard_snackbar)
                        )
                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    is SnackbarState.None -> Unit

                    is SnackbarState.IPProtectionShowActionSettingsSnackbar -> showActionSettingsSnackbar(state.title)

                    is SnackbarState.IPProtectionShowSnackbar -> showIPProtectionSnackBar(state.title)
                }
            }
    }

    private fun navigateToTabsTrayWithGroup(groupId: String) {
        navController.navigate(
            BrowserFragmentDirections.actionGlobalTabManagementFragment(
                enterMultiselect = false,
                accessPoint = AccessPoint.None, // Not setting this for now, since this will be removed for strip
                page = Page.NormalTabs,
                groupId = groupId,
            )
        )
    }

    private fun showBookmarkAddedSnackbarFor(state: SnackbarState.BookmarkAdded) {
        Result.runCatching {
                // We don't get smart compiler casts if we check these for nullity, so we'll just
                // use runCatching to short-circuit. Since guidToEdit wouldn't get hit until the lambda
                // invocation, we'll need to test them early.
                val guidToEdit = state.guidToEdit!!
                val parentNode = state.parentNode!!
                snackbarDelegate.show(
                    text =
                        context.getString(
                            R.string.bookmark_saved_in_folder_snackbar,
                            friendlyRootTitle(context, parentNode),
                        ),
                    duration = Snackbar.LENGTH_LONG,
                    action = context.getString(R.string.edit_bookmark_snackbar_action),
                ) { view ->
                    navController.navigateWithBreadcrumb(
                        directions =
                            BrowserFragmentDirections.actionGlobalBookmarkEditFragment(
                                guidToEdit = guidToEdit,
                                requiresSnackbarPaddingForToolbar = true,
                            ),
                        navigateFrom = "BrowserFragment",
                        navigateTo = "ActionGlobalBookmarkEditFragment",
                        crashReporter = view.context.components.analytics.crashReporter,
                    )
                }
            }
            .onFailure {
                snackbarDelegate.show(
                    text = R.string.bookmark_invalid_url_error,
                    duration = Snackbar.LENGTH_LONG,
                )
            }

        appStore.dispatch(SnackbarAction.SnackbarShown)
    }

    private fun showBookmarkResultSnackbar(state: SnackbarState.BookmarkOperationResultReported) {
        val id =
            when (state.result) {
                BookmarksGlobalResultReport.EditBookmarkFailed -> R.string.bookmark_error_edit_bookmark
                BookmarksGlobalResultReport.SelectFolderFailed -> R.string.bookmark_error_select_folder
                BookmarksGlobalResultReport.AddFolderFailed -> R.string.bookmark_error_add_folder
                BookmarksGlobalResultReport.EditFolderFailed -> R.string.bookmark_error_edit_folder
            }
        snackbarDelegate.show(
            text = context.getString(id),
            duration = Snackbar.LENGTH_LONG,
        )
    }

    /**
     * The state could be consumed by [IPProtectionSnackbarBinding] as well (e.g. three dot menu or trust panel opened),
     * in which case, to avoid showing snackbar twice, we only show it here if the view is active.
     */
    private fun showActionSettingsSnackbar(title: String) {
        if (viewHasFocus()) {
            snackbarDelegate.show(
                text = title,
                duration = context.components.settings.getSnackbarTimeout(hasAction = true).value.toInt(),
                action = context.getString(R.string.ip_protection_open_settings_snackbar_action),
            ) {
                navController.navigate(
                    BrowserFragmentDirections.actionGlobalIpProtectionFragment(
                        entrypoint = FenixFxAEntryPoint.IPProtectionSettings
                    )
                )
            }

            appStore.dispatch(SnackbarAction.SnackbarShown)
        }
    }

    /**
     * The state could be consumed by [IPProtectionSnackbarBinding] as well (e.g. three dot menu or trust panel opened),
     * in which case, to avoid showing snackbar twice, we only show it here if the view is active.
     */
    private fun showIPProtectionSnackBar(title: String) {
        if (viewHasFocus()) {
            snackbarDelegate.show(
                text = title,
                duration = context.components.settings.getSnackbarTimeout().value.toInt(),
            )

            appStore.dispatch(SnackbarAction.SnackbarShown)
        }
    }
}
