/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser

import androidx.annotation.IdRes
import androidx.navigation.NavDirections
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuFragmentDirections
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.ext.canGoBackInHistoryOrToStories
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.utils.Stories.hasUrlOfAHomeScreenStory
import org.mozilla.fenix.utils.Stories.hasUrlOfAStoriesScreenStory
import org.mozilla.fenix.utils.Stories.isUrlOfAStoriesScreenStory

/**
 * [MenuItemProvider] for the menu item allowing to navigate back.
 *
 * @param browserStore [BrowserStore] used to know if the current page can navigate back.
 * @param target [MenuTarget] for which this menu item would be shown for.
 * @param goBack [SessionUseCases.GoBackUseCase] for navigating back in the current tab.
 * @param settings [Settings] application settings.
 * @param scope [CoroutineScope] used to keep the item up to date for as long as it can be shown.
 */
class BackMenuItemProvider(
    private val browserStore: BrowserStore,
    private val target: MenuTarget,
    private val goBack: SessionUseCases.GoBackUseCase,
    private val settings: Settings,
    scope: CoroutineScope,
) : MenuItemProvider {
    override val itemsFlow: StateFlow<List<MenuItem>> =
        if (target is MenuTarget.CustomizeMenu) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM))
        } else if (target is MenuTarget.Preview) {
            MutableStateFlow(
                listOf(DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.Navigate.Back(viewHistory = false)))
            )
        } else {
            browserStore.stateFlow
                .map { state ->
                    target.browserSessionFrom(state)?.canGoBackInHistoryOrToStories() ?: false
                }
                .map { listOf(it.toMenuItem()) }
                .stateIn(
                    scope = scope,
                    started = SharingStarted.Eagerly,
                    initialValue =
                        listOf(
                            (target.browserSessionFrom(browserStore.state)?.canGoBackInHistoryOrToStories() ?: false)
                                .toMenuItem()
                        ),
                )
        }

    override fun handles(event: MenuEvent) = target !is MenuTarget.Preview && (event is MenuAction.Navigate.Back)

    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        if (event !is MenuAction.Navigate.Back) return
        val session = target.browserSessionFrom(browserStore.state) ?: return

        if (event.viewHistory) {
            menu.navigate(
                NavGraphDirections.actionGlobalTabHistoryDialogFragment(
                    activeSessionId = (target as? MenuTarget.CustomTab)?.customTabId
                )
            )
        } else {
            navigateBack(session, menu)
        }
    }

    private fun navigateBack(session: SessionState, menu: MenuHost) {
        when {
            target is MenuTarget.CustomTab -> goBackInHistory(session, menu)
            settings.enableHomepageAsNewTab -> {
                val isBackToStoriesScreen =
                    session.content.url.isUrlOfAStoriesScreenStory() && menu.navigateBackTo(R.id.storiesFragment)
                if (!isBackToStoriesScreen) {
                    goBackInHistory(session, menu)
                }
            }
            session.hasUrlOfAHomeScreenStory() ->
                menu.goBackTo(R.id.homeFragment, NavGraphDirections.actionGlobalHome())
            session.hasUrlOfAStoriesScreenStory() ->
                menu.goBackTo(R.id.storiesFragment, MenuFragmentDirections.actionMenuFragmentToStoriesFragment())
            else -> goBackInHistory(session, menu)
        }
    }

    private fun goBackInHistory(session: SessionState, menu: MenuHost) {
        menu.dismiss()
        goBack(tabId = session.id)
    }

    /** Prefer showing the existing [destinationId] screen, which keeps its scroll position. */
    private fun MenuHost.goBackTo(@IdRes destinationId: Int, directions: NavDirections) {
        if (!navigateBackTo(destinationId)) {
            navigate(directions)
        }
    }

    private fun Boolean.toMenuItem(): MenuItem {
        val canGoBack = this

        return DEFAULT_MENU_ITEM.copy(
            onClickEvent = MenuAction.Navigate.Back(viewHistory = false),
            onLongClickEvent = MenuAction.Navigate.Back(viewHistory = true),
            state = if (canGoBack) MenuItemState.DEFAULT else MenuItemState.DISABLED,
        )
    }

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_back)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_back_24),
            )
    }
}
