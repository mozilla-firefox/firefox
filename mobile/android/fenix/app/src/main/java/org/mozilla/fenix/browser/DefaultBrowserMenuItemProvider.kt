/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
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
 * @param scope [CoroutineScope] used to keep the item up to date for as long as it can be shown.
 * @param setAsDefaultBrowser delegate for setting the current browser as the default.
 */
class DefaultBrowserMenuItemProvider(
    private val settings: Settings,
    appName: String,
    private val target: MenuTarget,
    scope: CoroutineScope,
    private val setAsDefaultBrowser: () -> Unit,
) : MenuItemProvider {
    private val banner =
        BannerMenuItem(
            title = MenuItemTitle(Text.Resource(R.string.browser_menu_default_banner_title, listOf(appName))),
            summary =
                MenuItemSummary(
                    text = Text.Resource(R.string.browser_menu_default_banner_subtitle_2),
                    maxLines = 3,
                ),
            icon = MenuItemIconRes(R.drawable.kit_default_browser_menu_banner),
            actionButtonText = Text.Resource(R.string.browser_menu_default_banner_button),
            onClickEvent = DefaultBrowserMenuBannerClicked,
            onDismissEvent = DefaultBrowserMenuBannerDismissed,
        )

    private val isShownWhenOpened = isBannerShown(settings)
    private val isDismissed = MutableStateFlow(!settings.shouldShowMenuBanner)

    override val itemsFlow: StateFlow<List<MenuItem>> =
        combine(isDismissed, settings.isDefaultBrowserFlow) { isDismissed, isDefaultBrowser ->
                itemsFor(isShown = isShownWhenOpened && !isDismissed && !isDefaultBrowser)
            }
            .stateIn(
                scope = scope,
                started = SharingStarted.Eagerly,
                initialValue = itemsFor(isShown = isShownWhenOpened),
            )

    private fun itemsFor(isShown: Boolean) = if (isShown) listOf(banner) else emptyList()

    override fun handles(event: MenuEvent) =
        target != MenuTarget.Preview &&
            (event == DefaultBrowserMenuBannerClicked || event == DefaultBrowserMenuBannerDismissed)

    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        if (event == DefaultBrowserMenuBannerClicked) {
            setAsDefaultBrowser()
        } else {
            settings.shouldShowMenuBanner = false
            isDismissed.value = true
        }
    }

    companion object {
        /**
         * Whether a newly opened menu shows the banner proposing to make this the default browser.
         *
         * @param settings [Settings] storing whether the banner was dismissed and whether this is the default browser.
         */
        fun isBannerShown(settings: Settings): Boolean = settings.shouldShowMenuBanner && !settings.isDefaultBrowser
    }
}
