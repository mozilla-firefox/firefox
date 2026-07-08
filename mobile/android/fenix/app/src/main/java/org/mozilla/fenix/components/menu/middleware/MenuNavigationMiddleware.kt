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
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.ext.getUrl
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.SessionState
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
import mozilla.components.service.fxa.manager.AccountState.Unknown
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.collections.SaveCollectionStep
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.components.menu.BrowserNavigationParams
import org.mozilla.fenix.components.menu.MenuDialogFragmentDirections
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.components.menu.store.MenuState
import org.mozilla.fenix.components.menu.store.MenuStore
import org.mozilla.fenix.components.menu.toFenixFxAEntryPoint
import org.mozilla.fenix.components.share.ShareSource
import org.mozilla.fenix.components.usecases.ShareUseCases
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.settings.SupportUtils.AMO_HOMEPAGE_FOR_ANDROID
import org.mozilla.fenix.share.ShareFragment
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.utils.Stories.hasUrlOfAHomeScreenStory
import org.mozilla.fenix.utils.Stories.hasUrlOfAStoriesScreenStory
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
 * @param shareUseCases [ShareUseCases] for sharing content via the system share sheet or the in-app [ShareFragment].
 * @param settings Used to check [Settings] when adding items to the home screen.
 * @param onDismiss Callback invoked to dismiss the menu dialog.
 * @param scope [CoroutineScope] used to launch coroutines.
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
    private val shareUseCases: ShareUseCases,
    private val settings: Settings,
    private val onDismiss: suspend () -> Unit,
    private val scope: CoroutineScope = CoroutineScope(Dispatchers.Main),
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

        scope.launch {
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

                        is Authenticating, NotAuthenticated, Unknown -> navController.nav(
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

                is MenuAction.Navigate.Wallpaper -> navController.nav(
                    R.id.menuDialogFragment,
                    MenuDialogFragmentDirections.actionGlobalWallpaperSettingsFragment(),
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
                    val session: SessionState? = currentState.browserMenuState?.selectedTab
                    val url = session?.getTabUrl()

                    shareUseCases.shareUrl(
                        id = session?.id,
                        url = url,
                        title = session?.content?.title,
                        source = if (session.isCustomTab()) {
                            ShareSource.CUSTOM_TAB_MENU
                        } else {
                            ShareSource.BROWSER_MENU
                        },
                        isPrivate = session?.content?.private ?: false,
                        isCustomTab = session.isCustomTab(),
                        navigateToShareFragment = {
                            val shareData = arrayOf(ShareData(title = session?.content?.title, url = url))
                            val popUpToId = if (session.isCustomTab()) {
                                R.id.externalAppBrowserFragment
                            } else {
                                R.id.browserFragment
                            }

                            navController.nav(
                                id = R.id.menuDialogFragment,
                                directions = MenuDialogFragmentDirections.actionGlobalShareFragment(
                                    sessionId = session?.id,
                                    data = shareData,
                                    showPage = true,
                                ),
                                navOptions = NavOptions.Builder()
                                    .setPopUpTo(popUpToId, false)
                                    .build(),
                            )
                        },
                    )

                    navController.popBackStack(R.id.menuDialogFragment, true)
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
                    val session = currentState.browserMenuState?.selectedTab
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
                                // active session here implies a custom tab. so we only set this if it's a custom tab
                                activeSessionId = currentState.browserMenuState?.selectedTab
                                    ?.takeIf { it.isCustomTab() }
                                    ?.id,
                            ),
                            navOptions = NavOptions.Builder()
                                .setPopUpTo(R.id.browserFragment, false)
                                .build(),
                        )
                    } else {
                        val session = currentState.browserMenuState?.selectedTab ?: return@launch

                        when {
                            settings.enableHomepageAsNewTab ->
                                browserStore.dispatch(EngineAction.GoBackAction(session.id))
                            !session.isCustomTab() && session.hasUrlOfAHomeScreenStory() -> {
                                // First attempting to go back to the existing home fragment
                                // to preserve its scroll position.
                                val popToExistingHomeFragment =
                                    navController.popBackStack(R.id.homeFragment, false)
                                if (!popToExistingHomeFragment) {
                                    navController.nav(
                                        id = R.id.menuDialogFragment,
                                        directions = NavGraphDirections.actionGlobalHome(),
                                    )
                                }
                            }
                            !session.isCustomTab() && session.hasUrlOfAStoriesScreenStory() -> {
                                // First attempting to go back to the existing stories fragment
                                // to preserve its scroll position.
                                val popToExistingStoriesFragment =
                                    navController.popBackStack(R.id.storiesFragment, false)
                                if (!popToExistingStoriesFragment) {
                                    navController.nav(
                                        id = R.id.menuDialogFragment,
                                        directions = MenuDialogFragmentDirections
                                            .actionMenuDialogFragmentToStoriesFragment(),
                                    )
                                }
                            }
                            else -> sessionUseCases.goBack.invoke(session.id)
                        }

                        onDismiss()
                    }
                }

                is MenuAction.Navigate.Forward -> {
                    val session = currentState.browserMenuState?.selectedTab
                    if (action.viewHistory) {
                        navController.nav(
                            id = R.id.menuDialogFragment,
                            directions = MenuDialogFragmentDirections.actionGlobalTabHistoryDialogFragment(
                                // active session here implies a custom tab. so we only set this if it's a custom tab
                                activeSessionId = currentState.browserMenuState?.selectedTab
                                    ?.takeIf { it.isCustomTab() }
                                    ?.id,
                            ),
                            navOptions = NavOptions.Builder()
                                .setPopUpTo(R.id.browserFragment, false)
                                .build(),
                        )
                    } else {
                        session?.let {
                            sessionUseCases.goForward.invoke(it.id)
                            onDismiss()
                        }
                    }
                }

                is MenuAction.Navigate.Reload -> {
                    val session = currentState.browserMenuState?.selectedTab

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
                    val session = currentState.browserMenuState?.selectedTab

                    session?.let {
                        sessionUseCases.stopLoading.invoke(it.id)
                        onDismiss()
                    }
                }

                is MenuAction.Navigate.IPProtectionSettings -> {
                    navController.nav(
                        id = R.id.menuDialogFragment,
                        directions = MenuDialogFragmentDirections.actionMenuDialogFragmentToIpProtectionFragment(
                            entrypoint = FenixFxAEntryPoint.IPProtectionMainMenu,
                        ),
                    )
                }

                else -> Unit
            }
        }
    }
}
