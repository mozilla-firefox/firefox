/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser

import androidx.navigation.NavDirections
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.middleware.getTabUrl
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.components.share.ShareSource
import org.mozilla.fenix.components.usecases.ShareUseCases

/**
 * [MenuItemProvider] for the menu item allowing to share the current page.
 *
 * @param browserStore [BrowserStore] used to get the current page.
 * @param target [MenuTarget] for which this menu item would be shown for.
 * @param shareUseCases [ShareUseCases] for sharing the current page.
 */
class ShareMenuItemProvider(
    private val browserStore: BrowserStore,
    private val target: MenuTarget,
    private val shareUseCases: ShareUseCases,
) : MenuItemProvider {
    override val itemsFlow: StateFlow<List<MenuItem>> =
        if (target == MenuTarget.CustomizeMenu) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM))
        } else {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.Navigate.Share)))
        }

    override fun handles(event: MenuEvent) = target != MenuTarget.Preview && (event == MenuAction.Navigate.Share)

    /**
     * The page is shared through the system share sheet when possible, with the menu closed afterwards. Otherwise the
     * menu is replaced with the screen for sharing it, which [ShareUseCases.shareUrl] asks for before returning.
     */
    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        val tab = target.browserSessionFrom(browserStore.state) ?: return
        val url = tab.getTabUrl()
        val shareData = ShareData(title = tab.content.title, url = url, private = tab.content.private)

        val isCustomTab = target is MenuTarget.CustomTab
        val source = if (isCustomTab) ShareSource.CUSTOM_TAB_MENU else ShareSource.BROWSER_MENU

        var shareScreen: NavDirections? = null
        shareUseCases.shareUrl(
            id = tab.id,
            url = url,
            title = tab.content.title,
            source = source,
            isPrivate = tab.content.private,
            isCustomTab = isCustomTab,
            navigateToShareFragment = {
                shareScreen =
                    NavGraphDirections.actionGlobalShareFragment(
                        data = arrayOf(shareData),
                        showPage = true,
                        sessionId = tab.id,
                    )
            },
        )

        shareScreen?.let { menu.navigate(it) } ?: menu.dismiss()
    }

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_share)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_share_android_24),
            )
    }
}
