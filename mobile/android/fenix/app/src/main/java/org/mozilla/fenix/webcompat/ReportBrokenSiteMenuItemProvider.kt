/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.webcompat

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.support.ktx.kotlin.isAboutUrl
import mozilla.components.support.ktx.kotlin.isContentUrl
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.menu.MenuFragmentDirections
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.middleware.getTabUrl
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.utils.Settings

/**
 * [MenuItemProvider] for the menu item allowing to report the current page as broken.
 *
 * @param browserStore [BrowserStore] allowing to integrate with the current open tabs.
 * @param target [MenuTarget] for which this menu item would be shown for.
 * @param settings [Settings] used to know whether the user allows telemetry.
 * @param webCompatReporterMoreInfoSender [WebCompatReporterMoreInfoSender] for sending the details of a broken site to
 *   webcompat.com.
 * @param appStore [AppStore] used to know whether to open pages in a private tab.
 * @param fenixBrowserUseCases [FenixBrowserUseCases] for opening webcompat.com.
 * @param scope [CoroutineScope] used to keep the item up to date for as long as it can be shown, and to send the
 *   details of a broken site.
 */
@Suppress("LongParameterList")
class ReportBrokenSiteMenuItemProvider(
    private val browserStore: BrowserStore,
    private val target: MenuTarget,
    private val settings: Settings,
    private val webCompatReporterMoreInfoSender: WebCompatReporterMoreInfoSender,
    private val appStore: AppStore,
    private val fenixBrowserUseCases: FenixBrowserUseCases,
    private val scope: CoroutineScope,
) : MenuItemProvider {
    override val itemsFlow: StateFlow<List<MenuItem>> =
        if (target == MenuTarget.CustomizeMenu) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM))
        } else if (target == MenuTarget.Preview) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.Navigate.WebCompatReporter)))
        } else {
            browserStore.stateFlow
                .map { state -> listOfNotNull(target.browserSessionFrom(state).reportBrokenSiteItem()) }
                .stateIn(
                    scope = scope,
                    started = SharingStarted.Eagerly,
                    initialValue = listOfNotNull(target.browserSessionFrom(browserStore.state).reportBrokenSiteItem()),
                )
        }

    override fun handles(event: MenuEvent) =
        target != MenuTarget.Preview && (event == MenuAction.Navigate.WebCompatReporter)

    /**
     * A broken site is reported from inside the app if the user allows telemetry, since only then can the details of
     * the issue be collected. If they don't, the report is filled in on webcompat.com, with the details of the issue
     * sent separately before opening the website, so that the engine still has the page to collect them from.
     */
    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        val tab = target.browserSessionFrom(browserStore.state) ?: return
        val tabUrl = tab.content.url

        if (settings.isTelemetryEnabled) {
            menu.navigate(MenuFragmentDirections.actionMenuFragmentToWebCompatReporterFragment(tabUrl = tabUrl))
            return
        }

        scope.launch {
            webCompatReporterMoreInfoSender.sendMoreWebCompatInfo(
                reason = null,
                problemDescription = null,
                enteredUrl = null,
                tabUrl = tab.getTabUrl(),
                engineSession = tab.engineState.engineSession,
            )

            menu.dismiss()
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = "$WEB_COMPAT_REPORTER_URL$tabUrl",
                newTab = true,
                private = appStore.state.mode.isPrivate,
            )
        }
    }

    private fun SessionState?.reportBrokenSiteItem(): MenuItem? {
        val url = this?.content?.url ?: return null

        return DEFAULT_MENU_ITEM.copy(
            onClickEvent = MenuAction.Navigate.WebCompatReporter,
            // There is nothing to report about the pages the browser itself shows.
            state =
                when (url.isAboutUrl() || url.isContentUrl()) {
                    true -> MenuItemState.DISABLED
                    else -> MenuItemState.DEFAULT
                },
        )
    }

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_webcompat_reporter_2)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_lightbulb_24),
            )
    }
}
