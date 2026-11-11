/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.summarization

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuFragmentDirections
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.summarization.eligibility.SummarizationEligibilityChecker
import org.mozilla.fenix.summarization.onboarding.SummarizationFeatureDiscoveryConfiguration
import org.mozilla.fenix.summarization.onboarding.SummarizeDiscoveryEvent

/**
 * [MenuItemProvider] for the menu item allowing to summarize the current page.
 *
 * Shown only where the feature is available, and enabled only for a page that can actually be summarized.
 *
 * @param browserStore [BrowserStore] allowing to integrate with the current open tabs.
 * @param target [MenuTarget] for which this menu item would be shown for.
 * @param summarizationSettings [SummarizationFeatureDiscoveryConfiguration] telling whether to offer the feature.
 * @param eligibilityChecker [SummarizationEligibilityChecker] used to know whether the page can be summarized.
 * @param scope [CoroutineScope] tied to the lifetime of the menu, on which to keep the item up to date and check
 *   whether the current page can be summarized.
 */
class SummarizePageMenuItemProvider(
    private val browserStore: BrowserStore,
    private val target: MenuTarget,
    private val summarizationSettings: SummarizationFeatureDiscoveryConfiguration,
    private val eligibilityChecker: SummarizationEligibilityChecker,
    private val scope: CoroutineScope,
) : MenuItemProvider {
    // The item is first offered as not usable, since knowing whether the page can be summarized means asking the
    // engine about it and the menu should not wait for that.
    private val isPageEligible = MutableStateFlow(false)

    override val itemsFlow: StateFlow<List<MenuItem>> =
        if (target == MenuTarget.CustomizeMenu) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM)).asStateFlow()
        } else if (target == MenuTarget.Preview) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.Navigate.Summarizer)))
                .asStateFlow()
        } else {
            combine(browserStore.stateFlow, isPageEligible) { state, isEligible ->
                    listOfNotNull(state.summarizePageItem(isPageEligible = isEligible))
                }
                .stateIn(
                    scope = scope,
                    started = SharingStarted.Eagerly,
                    initialValue =
                        listOfNotNull(browserStore.state.summarizePageItem(isPageEligible = isPageEligible.value)),
                )
        }

    init {
        if (target != MenuTarget.CustomizeMenu && target != MenuTarget.Preview) {
            scope.launch { resolvePageEligibility() }
        }
    }

    private suspend fun resolvePageEligibility() {
        val session = target.browserSessionFrom(browserStore.state)?.engineState?.engineSession ?: return

        isPageEligible.value = eligibilityChecker.checkLanguage(session).getOrDefault(false)
    }

    override fun handles(event: MenuEvent) =
        target != MenuTarget.Preview &&
            (event == MenuAction.Navigate.Summarizer || event == MenuAction.OnSummarizationMenuExposed)

    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        when (event) {
            MenuAction.Navigate.Summarizer -> {
                val tabId = target.browserSessionFrom(browserStore.state)?.id ?: return
                menu.navigate(MenuFragmentDirections.actionMenuFragmentToSummarizationFragment(sessionId = tabId))
            }

            MenuAction.OnSummarizationMenuExposed -> scope.launch { recordExposure() }

            else -> Unit
        }
    }

    /** Being shown counts towards the user discovering the feature only where it could actually be used. */
    private suspend fun recordExposure() {
        if (!summarizationSettings.showMenuItem) return
        val tab = target.browserSessionFrom(browserStore.state)?.takeIf { !it.content.private } ?: return
        val session = tab.engineState.engineSession ?: return

        if (eligibilityChecker.checkLanguage(session).getOrDefault(false)) {
            summarizationSettings.cacheDiscoveryEvent(SummarizeDiscoveryEvent.MenuItemExposure)
        }
    }

    /** Summarizing is only offered where the feature is available, and only usable for a normal page. */
    private fun BrowserState.summarizePageItem(isPageEligible: Boolean): MenuItem? {
        if (!summarizationSettings.showMenuItem) return null

        // Private browsing is not something to send off to be summarized.
        val isNormalTab = target.browserSessionFrom(this)?.content?.private == false

        return summarizePageItem(
            isEnabled = isNormalTab && isPageEligible,
            isHighlighted = summarizationSettings.shouldHighlightMenuItem && isNormalTab,
        )
    }

    private fun summarizePageItem(
        isEnabled: Boolean,
        isHighlighted: Boolean,
    ) =
        DEFAULT_MENU_ITEM.copy(
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_lightning_24, isHighlighted = isHighlighted),
            onClickEvent = MenuAction.Navigate.Summarizer,
            onShownEvent = MenuAction.OnSummarizationMenuExposed,
            showNewIndicator = true,
            state =
                when (isEnabled) {
                    true -> MenuItemState.DEFAULT
                    else -> MenuItemState.DISABLED
                },
        )

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_summarize_page)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_lightning_24),
            )
    }
}

/** Whether this menu item is the summarization one. */
internal fun StandardMenuItem.isSummarizePageMenuItem() = onClickEvent == MenuAction.Navigate.Summarizer
