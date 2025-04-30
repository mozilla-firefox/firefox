/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.snackbar

import android.content.Context
import androidx.navigation.NavController
import com.google.android.material.snackbar.Snackbar
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.DelicateCoroutinesApi
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.GlobalScope
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.browser.state.selector.findCustomTabOrSelectedTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.accounts.push.SendTabUseCases
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.lib.state.helpers.AbstractBinding
import mozilla.components.ui.widgets.SnackbarDelegate
import org.mozilla.fenix.GleanMetrics.SentFromFirefox
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.ShareAction
import org.mozilla.fenix.components.appstate.AppAction.SnackbarAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.navigateWithBreadcrumb
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.ext.tabClosedUndoMessage
import org.mozilla.fenix.library.bookmarks.friendlyRootTitle

const val WEBCOMPAT_SNACKBAR_DURATION_MS = 20000

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
 * @param customTabSessionId Optional custom tab session ID if navigating from a custom tab or null
 * if the selected session should be used.
 * @param ioDispatcher The [CoroutineDispatcher] used for background operations executed when
 * the user starts a snackbar action.
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
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : AbstractBinding<AppState>(appStore) {

    private val currentSession
        get() = browserStore.state.findCustomTabOrSelectedTab(customTabSessionId)

    @Suppress("LongMethod", "ComplexMethod")
    override suspend fun onState(flow: Flow<AppState>) {
        flow.map { state -> state.snackbarState }
            .distinctUntilChanged()
            .collect { state ->
                when (state) {
                    is SnackbarState.BookmarkAdded -> {
                        showBookmarkAddedSnackbarFor(state)
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

                    is SnackbarState.ShortcutRemoved -> {
                        snackbarDelegate.show(
                            text = R.string.snackbar_top_site_removed,
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
                                    preferenceToScrollTo = context.getString(R.string.pref_key_link_sharing),
                                ),
                            )
                            navController.navigate(
                                BrowserFragmentDirections.actionGlobalLinkSharingFragment(),
                            )
                        }

                        context.settings().linkSharingSettingsSnackbarShown = true
                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    is SnackbarState.SharedTabsSuccessfully -> {
                        snackbarDelegate.show(
                            text = when (state.tabs.size) {
                                1 -> R.string.sync_sent_tab_snackbar
                                else -> R.string.sync_sent_tabs_snackbar
                            },
                            duration = Snackbar.LENGTH_SHORT,
                        )

                        appStore.dispatch(SnackbarAction.SnackbarShown)
                    }

                    is SnackbarState.ShareTabsFailed -> {
                        @OptIn(DelicateCoroutinesApi::class)
                        snackbarDelegate.show(
                            text = R.string.sync_sent_tab_error_snackbar,
                            duration = Snackbar.LENGTH_LONG,
                            isError = true,
                            action = R.string.sync_sent_tab_error_snackbar_action,
                        ) {
                            sendTabUseCases ?: return@show

                            GlobalScope.launch(ioDispatcher) {
                                val operation = when (state.destination.size) {
                                    1 -> sendTabUseCases.sendToDeviceAsync(
                                        deviceId = state.destination[0],
                                        tabs = state.tabs,
                                    )
                                    else -> sendTabUseCases.sendToAllAsync(
                                        tabs = state.tabs,
                                    )
                                }
                                when (operation.await()) {
                                    true -> appStore.dispatch(
                                        ShareAction.SharedTabsSuccessfully(
                                            destination = state.destination,
                                            tabs = state.tabs,
                                        ),
                                    )

                                    false -> appStore.dispatch(
                                        ShareAction.ShareTabsFailed(
                                            destination = state.destination,
                                            tabs = state.tabs,
                                        ),
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
                            text = context.getString(R.string.webcompat_reporter_success_snackbar_text),
                            duration = WEBCOMPAT_SNACKBAR_DURATION_MS,
                            action = context.getString(R.string.webcompat_reporter_dismiss_success_snackbar_text),
                            listener = { snackbarDelegate.dismiss() },
                        )
                    }

                    SnackbarState.SiteDataCleared -> {
                        snackbarDelegate.show(
                            text = R.string.clear_site_data_snackbar,
                            duration = Snackbar.LENGTH_LONG,
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

                    SnackbarState.None -> Unit
                }
            }
    }

    private fun showBookmarkAddedSnackbarFor(state: SnackbarState.BookmarkAdded) {
        Result.runCatching {
            // We don't get smart compiler casts if we check these for nullity, so we'll just
            // use runCatching to short-circuit. Since guidToEdit wouldn't get hit until the lambda
            // invocation, we'll need to test them early.
            val guidToEdit = state.guidToEdit!!
            val parentNode = state.parentNode!!
            snackbarDelegate.show(
                text = context.getString(
                    R.string.bookmark_saved_in_folder_snackbar,
                    friendlyRootTitle(context, parentNode),
                ),
                duration = Snackbar.LENGTH_LONG,
                action = context.getString(R.string.edit_bookmark_snackbar_action),
            ) { view ->
                navController.navigateWithBreadcrumb(
                    directions = BrowserFragmentDirections.actionGlobalBookmarkEditFragment(
                        guidToEdit = guidToEdit,
                        requiresSnackbarPaddingForToolbar = true,
                    ),
                    navigateFrom = "BrowserFragment",
                    navigateTo = "ActionGlobalBookmarkEditFragment",
                    crashReporter = view.context.components.analytics.crashReporter,
                )
            }
        }.onFailure {
            snackbarDelegate.show(
                text = R.string.bookmark_invalid_url_error,
                duration = Snackbar.LENGTH_LONG,
            )
        }

        appStore.dispatch(SnackbarAction.SnackbarShown)
    }
}
