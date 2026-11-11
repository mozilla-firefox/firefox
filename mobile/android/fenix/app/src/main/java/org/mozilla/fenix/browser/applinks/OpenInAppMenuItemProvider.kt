/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.applinks

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.feature.app.links.AppLinksUseCases
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.utils.Settings

/**
 * [MenuItemProvider] for the menu item allowing to open the current webpage in the app that handles it.
 *
 * The item is always shown, but usable only while there actually is such an app, so that it does not appear and
 * disappear as the user browses.
 *
 * @param browserStore [BrowserStore] allowing to integrate with the current open tabs.
 * @param target [MenuTarget] for which this menu item would be shown for.
 * @param appStore [AppStore] used to know whether to draw attention to this item.
 * @param appLinksUseCases [AppLinksUseCases] used to know which app can open the current page, and to open it there.
 * @param settings [Settings] for remembering that the user opened a page in an app, so that they are not told about it
 *   again.
 * @param scope [CoroutineScope] used to keep the item up to date for as long as it can be shown.
 */
class OpenInAppMenuItemProvider(
    private val browserStore: BrowserStore,
    private val target: MenuTarget,
    appStore: AppStore,
    private val appLinksUseCases: AppLinksUseCases,
    private val settings: Settings,
    scope: CoroutineScope,
) : MenuItemProvider {
    override val itemsFlow: StateFlow<List<MenuItem>> =
        if (target == MenuTarget.CustomizeMenu) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM))
        } else if (target == MenuTarget.Preview) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.OpenInApp)))
        } else {
            combine(browserStore.currentUrl(target), appStore.isOpenInAppHighlighted()) { url, isHighlighted ->
                    listOf(openInAppItem(url = url, isHighlighted = isHighlighted))
                }
                .stateIn(
                    scope = scope,
                    started = SharingStarted.Eagerly,
                    initialValue =
                        listOf(
                            openInAppItem(
                                url = target.browserSessionFrom(browserStore.state)?.content?.url,
                                isHighlighted = appStore.state.isOpenInAppHighlighted(),
                            )
                        ),
                )
        }

    /**
     * The item to show, usable only if an external app can open [url].
     *
     * Which app that is - if any - is asked of the system, which the use case keeps cheap through a short lived cache.
     */
    private fun openInAppItem(
        url: String?,
        isHighlighted: Boolean,
    ): MenuItem {
        val externalApp = url?.let { appLinksUseCases.appLinkRedirect(it) }?.takeIf { it.hasExternalApp() }

        return DEFAULT_MENU_ITEM.copy(
            title =
                MenuItemTitle(
                    when (val appName = externalApp?.appName?.takeIf { it.isNotEmpty() }) {
                        null -> Text.Resource(R.string.browser_menu_open_app_link)
                        else -> Text.Resource(R.string.browser_menu_open_in_fenix, listOf(appName))
                    }
                ),
            icon =
                MenuItemIconRes(
                    iconsR.drawable.mozac_ic_more_grid_24,
                    isHighlighted = isHighlighted && externalApp != null,
                ),
            onClickEvent = MenuAction.OpenInApp,
            state =
                when (externalApp) {
                    null -> MenuItemState.DISABLED
                    else -> MenuItemState.DEFAULT
                },
        )
    }

    override fun handles(event: MenuEvent) = target != MenuTarget.Preview && (event == MenuAction.OpenInApp)

    /**
     * Whether there is an app for the current page is resolved again, since the user may have navigated to another page
     * since the item was offered.
     */
    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        val url = target.browserSessionFrom(browserStore.state)?.content?.url ?: return
        val redirect = appLinksUseCases.appLinkRedirect(url)
        if (!redirect.hasExternalApp()) return

        settings.openInAppOpened = true
        appLinksUseCases.openAppLink(redirect.appIntent)
        menu.dismiss()
    }

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_open_app_link)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_more_grid_24),
            )
    }
}

private fun BrowserStore.currentUrl(target: MenuTarget) =
    stateFlow.map { state -> target.browserSessionFrom(state)?.content?.url }.distinctUntilChanged()

/** Whether attention should be drawn to opening the current page in an app, offered anew only when it changes. */
private fun AppStore.isOpenInAppHighlighted() =
    stateFlow.map { state -> state.isOpenInAppHighlighted() }.distinctUntilChanged()

private fun AppState.isOpenInAppHighlighted() =
    supportedMenuNotifications.contains(SupportedMenuNotifications.OpenInApp)
