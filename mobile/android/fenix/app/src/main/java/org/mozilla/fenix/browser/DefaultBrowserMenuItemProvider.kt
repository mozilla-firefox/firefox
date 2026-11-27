/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.BannerMenuItem
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemSummary
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction.DefaultBrowserMenuBannerClicked
import org.mozilla.fenix.components.menu.store.MenuAction.DefaultBrowserMenuBannerDismissed
import org.mozilla.fenix.utils.Settings

/**
 * [MenuItemProvider] for the menu item allowing to set the current browser as the default browser.
 *
 * @param settings [Settings] application settings from which to read or update whether the item should be shown.
 * @param appName The name of this application, shown in the item.
 * @param target [MenuTarget] for which this menu item would be shown for.
 * @param setAsDefaultBrowser delegate for setting the current browser as the default.
 */
class DefaultBrowserMenuItemProvider(
    private val settings: Settings,
    appName: String,
    private val target: MenuTarget,
    private val setAsDefaultBrowser: () -> Unit,
) : MenuItemProvider {
    private val items: MutableStateFlow<List<MenuItem>> =
        MutableStateFlow(
            listOfNotNull(
                when (settings.shouldShowMenuBanner && !settings.isDefaultBrowser) {
                    true ->
                        BannerMenuItem(
                            title =
                                MenuItemTitle(
                                    Text.Resource(R.string.browser_menu_default_banner_title, listOf(appName))
                                ),
                            summary =
                                MenuItemSummary(
                                    text = Text.Resource(R.string.browser_menu_default_banner_subtitle_2),
                                    maxLines = 3,
                                ),
                            icon = MenuItemIconRes(R.drawable.firefox_as_default_banner_illustration),
                            onClickEvent = DefaultBrowserMenuBannerClicked,
                            onDismissEvent = DefaultBrowserMenuBannerDismissed,
                        )
                    else -> null
                }
            )
        )

    override val itemsFlow: StateFlow<List<MenuItem>> = items.asStateFlow()

    override fun handles(event: MenuEvent) =
        target != MenuTarget.Preview &&
            (event == DefaultBrowserMenuBannerClicked || event == DefaultBrowserMenuBannerDismissed)

    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        if (event == DefaultBrowserMenuBannerClicked) {
            setAsDefaultBrowser()
        } else {
            settings.shouldShowMenuBanner = false
            items.value = emptyList()
        }
    }
}
