/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.translations

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemBadge
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.concept.engine.translate.findLanguage
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuFragmentDirections
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.nimbus.FxNimbus

/**
 * [MenuItemProvider] for the menu item allowing to translate the current page.
 *
 * Shown only where translating is actually available, and showing the language the page is already translated to.
 *
 * @param browserStore [BrowserStore] used to know whether the current page can be or already is translated, and to get
 *   the current page to translate.
 * @param target [MenuTarget] for which this menu item would be shown for.
 * @param translationsSettings [TranslationsEnabledSettings] whether the user turned translations off.
 * @param scope [CoroutineScope] used to keep the item up to date for as long as it can be shown.
 * @param isFeatureEnabled whether the translations feature is enabled.
 */
class TranslationsMenuItemProvider(
    private val browserStore: BrowserStore,
    private val target: MenuTarget,
    translationsSettings: TranslationsEnabledSettings,
    scope: CoroutineScope,
    private val isFeatureEnabled: Boolean = FxNimbus.features.translations.value().mainFlowBrowserMenuEnabled,
) : MenuItemProvider {
    override val itemsFlow: StateFlow<List<MenuItem>> =
        if (target == MenuTarget.CustomizeMenu) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM))
        } else if (target == MenuTarget.Preview) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.Navigate.Translate)))
        } else {
            combine(browserStore.stateFlow, translationsSettings.isEnabled) { state, isEnabled ->
                    listOfNotNull(state.translationsItem(isEnabled = isEnabled))
                }
                .stateIn(
                    scope = scope,
                    started = SharingStarted.Eagerly,
                    // Whether the user turned translations off is only known after reading it from disk, until then
                    // they are assumed to still be on - as in the menu this replaces. Nothing is shown anyway for as
                    // long as the engine has not reported that it supports this device.
                    initialValue = listOfNotNull(browserStore.state.translationsItem(isEnabled = true)),
                )
        }

    /** Translating is only offered for a page that can be translated, and to a user that has the feature enabled. */
    private fun BrowserState.translationsItem(isEnabled: Boolean): MenuItem? {
        if (!isEnabled || !isFeatureEnabled || translationEngine.isEngineSupported != true) return null

        val tab = target.browserSessionFrom(this) ?: return null

        return when (tab.translationsState.isTranslated) {
            true -> translatedItem(language = translatedLanguage(tab))
            // Reader view shows a stripped down version of the page and a PDF is not a page at all.
            else -> translatableItem(canTranslate = !tab.isReaderViewActive() && !tab.content.isPdf)
        }
    }

    /** The language the current page was translated to, named the way the user would name it. */
    private fun BrowserState.translatedLanguage(tab: SessionState): String? {
        val translatedTo =
            tab.translationsState.translationEngineState?.requestedTranslationPair?.toLanguage ?: return null

        return translationEngine.supportedLanguages?.findLanguage(translatedTo)?.localizedDisplayName
    }

    override fun handles(event: MenuEvent) = target != MenuTarget.Preview && (event == MenuAction.Navigate.Translate)

    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        val tabId = target.browserSessionFrom(browserStore.state)?.id ?: return

        menu.navigate(MenuFragmentDirections.actionMenuFragmentToTranslationsDialogFragment(sessionId = tabId))
    }

    private fun translatedItem(language: String?) =
        DEFAULT_MENU_ITEM.copy(
            title = MenuItemTitle(Text.Resource(R.string.browser_menu_translated)),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_translate_active_24),
            onClickEvent = MenuAction.Navigate.Translate,
            badge = language?.let { MenuItemBadge(text = Text.String(it), state = MenuItemState.ACTIVE) },
            state = MenuItemState.ACTIVE,
        )

    private fun translatableItem(canTranslate: Boolean) =
        DEFAULT_MENU_ITEM.copy(
            onClickEvent = MenuAction.Navigate.Translate,
            state =
                when (canTranslate) {
                    true -> MenuItemState.DEFAULT
                    else -> MenuItemState.DISABLED
                },
        )

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_translate_page_2)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_translate_24),
            )
    }
}

private fun SessionState.isReaderViewActive() = (this as? TabSessionState)?.readerState?.active == true
