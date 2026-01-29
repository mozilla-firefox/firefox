/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.middleware

import androidx.navigation.NavController
import androidx.navigation.NavOptions
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.browser.state.action.ShareResourceAction
import mozilla.components.browser.state.ext.getUrl
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.CustomTabSessionState
import mozilla.components.browser.state.state.content.ShareResourceState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession.LoadUrlFlags
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.feature.pwa.WebAppUseCases
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.components.service.fxa.manager.AccountState.Authenticated
import mozilla.components.service.fxa.manager.AccountState.Authenticating
import mozilla.components.service.fxa.manager.AccountState.AuthenticationProblem
import mozilla.components.service.fxa.manager.AccountState.NotAuthenticated
import mozilla.components.support.ktx.kotlin.isContentUrl
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.collections.SaveCollectionStep
import org.mozilla.fenix.components.menu.BrowserNavigationParams
import org.mozilla.fenix.components.menu.MenuDialogFragmentDirections
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.components.menu.store.MenuState
import org.mozilla.fenix.components.menu.store.MenuStore
import org.mozilla.fenix.components.menu.toFenixFxAEntryPoint
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.settings.SupportUtils.AMO_HOMEPAGE_FOR_ANDROID
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.webcompat.WEB_COMPAT_REPORTER_URL
import org.mozilla.fenix.webcompat.WebCompatReporterMoreInfoSender

/**
 * [Middleware] implementation for handling navigating events based on [MenuAction]s that are
 * dispatched to the [MenuStore].
 *
 * @param browserStore [BrowserStore] used to dispatch actions related to the menu state and access
 * the selected tab.
 * @param navController [NavController] used for navigation.
 * @param openToBrowser Callback to open the provided [BrowserNavigationParams]
 * in a new browser tab.
 * @param sessionUseCases [SessionUseCases] used to reload the page and navigate back/forward.
 * @param webAppUseCases [WebAppUseCases] used for adding items to the home screen.
 * @param settings Used to check [Settings] when adding items to the home screen.
 * @param onDismiss Callback invoked to dismiss the menu dialog.
 * @param scope [CoroutineScope] used to launch coroutines.
 * @param customTab [CustomTabSessionState] used for sharing custom tab.
 * @param webCompatReporterMoreInfoSender [WebCompatReporterMoreInfoSender] used
 * to send WebCompat info to webcompat.com.
 */
@Suppress("LongParameterList")
class MenuNavigationMiddleware(
    private val browserStore: BrowserStore,
    private val navController: NavController,
    private val openToBrowser: (params: BrowserNavigationParams) -> Unit,
    private val sessionUseCases: SessionUseCases,
    private val webAppUseCases: WebAppUseCases,
    private val settings: Settings,
    private val onDismiss: suspend () -> Unit,
    private val scope: CoroutineScope = CoroutineScope(Dispatchers.Main),
    private val customTab: CustomTabSessionState?,
    private val webCompatReporterMoreInfoSender: WebCompatReporterMoreInfoSender,
) : Middleware<MenuState, MenuAction> {

    @Suppress("CyclomaticComplexMethod", "LongMethod", "CognitiveComplexMethod")
    override fun invoke(
        store: Store<MenuState, MenuAction>,
        next: (MenuAction) -> Unit,
        action: MenuAction,
    ) {
        // Get the current state before further processing of the chain of actions.
        // This is to ensure that any navigation action will be using correct
        // state properties before they are modified due to other actions being
        // dispatched and processes.
        val currentState = store.state

        next(action)

        scope.launch(Dispatchers.Main) {
            when (action) {
                is MenuAction.Navigate.MozillaAccount -> {
                    when (action.accountState) {
                        Authenticated -> navController.nav(
                            R.id.menuDialogFragment,
                            MenuDialogFragmentDirections.actionGlobalAccountSettingsFragment(),
                        )

                        AuthenticationProblem -> navController.nav(
                            R.id.menuDialogFragment,
                            MenuDialogFragmentDirections.actionGlobalAccountProblemFragment(
                                entrypoint = action.accesspoint.toFenixFxAEntryPoint(),
                            ),
                        )

                        is Authenticating, NotAuthenticated -> navController.nav(
                            R.id.menuDialogFragment,
                            MenuDialogFragmentDirections.actionGlobalTurnOnSync(
                                entrypoint = action.accesspoint.toFenixFxAEntryPoint(),
                            ),
                        )
                    }
                }

                is MenuAction.Navigate.Settings -> navController.nav(
                    R.id.menuDialogFragment,
                    MenuDialogFragmentDirections.actionGlobalSettingsFragment(),
                )

                is MenuAction.Navigate.InstalledAddonDetails -> navController.nav(
                    R.id.menuDialogFragment,
                    MenuDialogFragmentDirections.actionMenuDialogFragmentToInstalledAddonDetailsFragment(
                        addon = action.addon,
                    ),
                )

                is MenuAction.Navigate.Bookmarks -> navController.nav(
                    R.id.menuDialogFragment,
                    MenuDialogFragmentDirections.actionGlobalBookmarkFragment(BookmarkRoot.Mobile.id),
                )

                is MenuAction.Navigate.History -> navController.nav(
                    R.id.menuDialogFragment,
                    MenuDialogFragmentDirections.actionGlobalHistoryFragment(),
                )

                is MenuAction.Navigate.Downloads -> navController.nav(
                    R.id.menuDialogFragment,
                    MenuDialogFragmentDirections.actionGlobalDownloadsFragment(),
                )

                is MenuAction.Navigate.Passwords -> navController.nav(
                    R.id.menuDialogFragment,
                    MenuDialogFragmentDirections.actionMenuDialogFragmentToLoginsListFragment(),
                )

                is MenuAction.Navigate.EditBookmark -> {
                    currentState.browserMenuState?.bookmarkState?.guid?.let { guidToEdit ->
                        navController.nav(
                            R.id.menuDialogFragment,
                            BrowserFragmentDirections.actionGlobalBookmarkEditFragment(
                                guidToEdit = guidToEdit,
                                requiresSnackbarPaddingForToolbar = true,
                            ),
                        )
                    }
                }

                is MenuAction.Navigate.AddToHomeScreen -> {
                    settings.installPwaOpened = true
                    if (webAppUseCases.isInstallable()) {
                        webAppUseCases.addToHomescreen()
                        onDismiss()
                    } else {
                        navController.nav(
                            R.id.menuDialogFragment,
                            MenuDialogFragmentDirections.actionMenuDialogFragmentToCreateShortcutFragment(),
                            navOptions = NavOptions.Builder()
                                .setPopUpTo(R.id.browserFragment, false)
                                .build(),
                        )
                    }
                }

                is MenuAction.Navigate.SaveToCollection -> {
                    currentState.browserMenuState?.selectedTab?.let { currentSession ->
                        navController.nav(
                            R.id.menuDialogFragment,
                            MenuDialogFragmentDirections.actionGlobalCollectionCreationFragment(
                                tabIds = arrayOf(currentSession.id),
                                selectedTabIds = arrayOf(currentSession.id),
                                saveCollectionStep = if (action.hasCollection) {
                                    SaveCollectionStep.SelectCollection
                                } else {
                                    SaveCollectionStep.NameCollection
                                },
                            ),
                            navOptions = NavOptions.Builder()
                                .setPopUpTo(R.id.browserFragment, false)
                                .build(),
                        )
                    }
                }

                is MenuAction.Navigate.Translate -> navController.nav(
                    R.id.menuDialogFragment,
                    MenuDialogFragmentDirections.actionMenuDialogFragmentToTranslationsDialogFragment(),
                    navOptions = NavOptions.Builder()
                        .setPopUpTo(R.id.browserFragment, false)
                        .build(),
                )

                is MenuAction.Navigate.Share -> {
                    val session = customTab ?: currentState.browserMenuState?.selectedTab
                    val url = customTab?.content?.url ?: currentState.browserMenuState?.selectedTab?.getUrl()

                    session?.let {
                        if (url?.isContentUrl() == true) {
                            browserStore.dispatch(
                                ShareResourceAction.AddShareAction(
                                    session.id,
                                    ShareResourceState.LocalResource(url),
                                ),
                            )
                            onDismiss()
                        } else {
                            val shareData = ShareData(title = it.content.title, url = url)
                            val direction = MenuDialogFragmentDirections.actionGlobalShareFragment(
                                sessionId = it.id,
                                data = arrayOf(shareData),
                                showPage = true,
                            )

                            val popUpToId = if (customTab != null) {
                                R.id.externalAppBrowserFragment
                            } else {
                                R.id.browserFragment
                            }

                            navController.nav(
                                R.id.menuDialogFragment,
                                direction,
                                navOptions = NavOptions.Builder()
                                    .setPopUpTo(popUpToId, false)
                                    .build(),
                            )
                        }
                    }
                }

                is MenuAction.Navigate.ManageExtensions -> navController.nav(
                    R.id.menuDialogFragment,
                    MenuDialogFragmentDirections.actionGlobalAddonsManagementFragment(),
                )

                is MenuAction.Navigate.DiscoverMoreExtensions -> openToBrowser(
                    BrowserNavigationParams(url = AMO_HOMEPAGE_FOR_ANDROID),
                )

                is MenuAction.Navigate.AddonDetails -> navController.nav(
                    R.id.menuDialogFragment,
                    MenuDialogFragmentDirections.actionMenuDialogFragmenToAddonDetailsFragment(
                        addon = action.addon,
                    ),
                )

                is MenuAction.Navigate.WebCompatReporter -> {
                    val session = customTab ?: currentState.browserMenuState?.selectedTab
                    session?.content?.url?.let { tabUrl ->
                        if (settings.isTelemetryEnabled) {
                            navController.nav(
                                id = R.id.menuDialogFragment,
                                directions = MenuDialogFragmentDirections
                                    .actionMenuDialogFragmentToWebCompatReporterFragment(
                                        tabUrl = tabUrl,
                                    ),
                            )
                        } else {
                            val selectedTab = browserStore.state.selectedTab

                            webCompatReporterMoreInfoSender.sendMoreWebCompatInfo(
                                reason = null,
                                problemDescription = null,
                                enteredUrl = null,
                                tabUrl = selectedTab?.getUrl(),
                                engineSession = selectedTab?.engineState?.engineSession,
                            )

                            openToBrowser(
                                BrowserNavigationParams(url = "$WEB_COMPAT_REPORTER_URL$tabUrl"),
                            )
                        }
                    }
                }

                is MenuAction.Navigate.Summarizer -> {
                    navController.nav(
                        id = R.id.menuDialogFragment,
                        directions = MenuDialogFragmentDirections
                            .actionMenuDialogFragmentToSummarizationFragment(),
                        navOptions = NavOptions.Builder()
                            .setPopUpTo(R.id.browserFragment, false)
                            .build(),
                    )
                }

                is MenuAction.Navigate.Back -> {
                    if (action.viewHistory) {
                        navController.nav(
                            id = R.id.menuDialogFragment,
                            directions = MenuDialogFragmentDirections.actionGlobalTabHistoryDialogFragment(
                                activeSessionId = currentState.customTabSessionId,
                            ),
                            navOptions = NavOptions.Builder()
                                .setPopUpTo(R.id.browserFragment, false)
                                .build(),
                        )
                    } else {
                        val session = customTab ?: currentState.browserMenuState?.selectedTab

                        session?.let {
                            sessionUseCases.goBack.invoke(it.id)
                            onDismiss()
                        }
                    }
                }

                is MenuAction.Navigate.Forward -> {
                    if (action.viewHistory) {
                        navController.nav(
                            id = R.id.menuDialogFragment,
                            directions = MenuDialogFragmentDirections.actionGlobalTabHistoryDialogFragment(
                                activeSessionId = currentState.customTabSessionId,
                            ),
                            navOptions = NavOptions.Builder()
                                .setPopUpTo(R.id.browserFragment, false)
                                .build(),
                        )
                    } else {
                        val session = customTab ?: currentState.browserMenuState?.selectedTab

                        session?.let {
                            sessionUseCases.goForward.invoke(it.id)
                            onDismiss()
                        }
                    }
                }

                is MenuAction.Navigate.Reload -> {
                    val session = customTab ?: currentState.browserMenuState?.selectedTab

                    session?.let {
                        sessionUseCases.reload.invoke(
                            tabId = it.id,
                            flags = if (action.bypassCache) {
                                LoadUrlFlags.select(LoadUrlFlags.BYPASS_CACHE)
                            } else {
                                LoadUrlFlags.none()
                            },
                        )
                        onDismiss()
                    }
                }

                is MenuAction.Navigate.Stop -> {
                    val session = customTab ?: currentState.browserMenuState?.selectedTab

                    session?.let {
                        sessionUseCases.stopLoading.invoke(it.id)
                        onDismiss()
                    }
                }

                else -> Unit
            }
        }
    }
}
