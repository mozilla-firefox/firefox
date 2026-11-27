/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import androidx.annotation.VisibleForTesting
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
import org.mozilla.fenix.FeatureFlags
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.DefaultBrowserMenuItemProvider
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration
import org.mozilla.fenix.components.menu.store.MenuAction.CustomizeMenuBannerClicked
import org.mozilla.fenix.components.menu.store.MenuAction.CustomizeMenuBannerDismissed
import org.mozilla.fenix.utils.Settings

@VisibleForTesting internal const val MAX_CUSTOMIZE_MENU_BANNER_VIEWS = 3

/**
 * The browser menu being built, as far as [CustomizeMenuBannerMenuItemProvider] needs to know to decide whether it
 * shows the banner promoting the menu customization.
 *
 * @property customizationInfo The user's customization of the browser menu, `null` if it was never customized.
 * @property customizedSectionsInfo The items of each section chosen by the user, keyed by section id, empty if they
 *   cannot be customized.
 * @property isRestored Whether the menu is restored from a saved state rather than newly opened.
 * @property wasShown Whether the menu showed the banner before being restored.
 * @property onViewCounted Invoked when a view of the banner is counted.
 * @property onShownDecided Invoked with whether the menu shows the banner, to be passed as [wasShown] if the menu gets
 *   restored.
 *
 * This is a temporary workaround for tracking how many times the banner was shown, so that it stops being shown after
 * [MAX_CUSTOMIZE_MENU_BANNER_VIEWS] views. It should be removed once the API from bug 2096717 is available.
 */
class CustomizeMenuBannerSession(
    val customizationInfo: CustomizedMenuInfo?,
    val customizedSectionsInfo: Map<String, CustomizedSectionInfo>,
    val isRestored: Boolean,
    val wasShown: Boolean,
    val onViewCounted: () -> Unit,
    val onShownDecided: (Boolean) -> Unit,
)

/**
 * [MenuItemProvider] for the promotional banner encouraging the user to customize the browser menu.
 *
 * The banner is only offered in the menu of a browser tab, described by a [session]. A newly opened menu offers it
 * while it is eligible, counting a view each time, up to [MAX_CUSTOMIZE_MENU_BANNER_VIEWS] views. A menu restored from
 * a saved state offers it only if it did before, the user didn't dismiss it since and the banner proposing to make this
 * the default browser isn't shown, without counting a view, so recreating the menu, for example on rotation, neither
 * counts it again nor brings back a dismissed banner.
 *
 * @param settings [Settings] storing whether the banner can still be shown and how many menus showed it.
 * @param target [MenuTarget] for which this menu item would be shown for.
 * @param isToolbarAtBottom Whether the toolbar is shown at the bottom of the screen.
 * @param session [CustomizeMenuBannerSession] describing the menu being built, `null` if it is not a browser menu.
 * @param isBannerFeatureEnabled Whether the banner may be shown at all in this build. If not, the provider neither
 *   offers it nor changes any of its settings.
 */
class CustomizeMenuBannerMenuItemProvider(
    private val settings: Settings,
    private val target: MenuTarget,
    private val isToolbarAtBottom: Boolean,
    session: CustomizeMenuBannerSession?,
    private val isBannerFeatureEnabled: Boolean = FeatureFlags.CUSTOMIZE_MENU_BANNER_ENABLED,
) : MenuItemProvider {
    private val items: MutableStateFlow<List<MenuItem>> =
        MutableStateFlow(if (session != null && isShownIn(session)) listOf(banner()) else emptyList())

    /** Whether the menu described by [session] shows the banner, reported to [session] to be saved for a restore. */
    private fun isShownIn(session: CustomizeMenuBannerSession): Boolean {
        if (target != MenuTarget.BrowserTab || !isBannerFeatureEnabled) return false

        val isShown =
            if (session.isRestored) {
                session.wasShown &&
                    settings.shouldShowCustomizeMenuBanner &&
                    !DefaultBrowserMenuItemProvider.isBannerShown(settings)
            } else {
                countViewIfEligible(session)
            }
        session.onShownDecided(isShown)
        return isShown
    }

    /**
     * Whether a newly opened menu shows the banner, counting a view if so. Also turns the banner off for good if the
     * menu is already customized while the banner could still be shown. The banner only shows when the one proposing to
     * make this the default browser doesn't, as that one takes priority.
     */
    private fun countViewIfEligible(session: CustomizeMenuBannerSession): Boolean {
        if (
            !settings.shouldShowCustomizeMenuBanner ||
                settings.customizeMenuBannerViewCount >= MAX_CUSTOMIZE_MENU_BANNER_VIEWS
        ) {
            return false
        }

        val isCustomized =
            BrowserMenuConfiguration.isCustomized(
                customizationInfo = session.customizationInfo,
                isToolbarAtBottom = isToolbarAtBottom,
                isExpandedToolbarEnabled = settings.shouldUseExpandedToolbar,
                customizedSectionsInfo = session.customizedSectionsInfo,
            )
        if (isCustomized) {
            settings.shouldShowCustomizeMenuBanner = false
        }

        val isEligible =
            !isCustomized &&
                settings.isMenuCustomizationScreenEnabled &&
                !DefaultBrowserMenuItemProvider.isBannerShown(settings)
        if (isEligible) {
            settings.customizeMenuBannerViewCount += 1
            session.onViewCounted()
        }
        return isEligible
    }

    private fun banner() =
        BannerMenuItem(
            title = MenuItemTitle(Text.Resource(R.string.browser_menu_customize_banner_title)),
            summary =
                MenuItemSummary(
                    text = Text.Resource(R.string.browser_menu_customize_banner_subtitle),
                    maxLines = 3,
                ),
            icon = MenuItemIconRes(R.drawable.customize_menu_banner_illustration),
            onClickEvent = CustomizeMenuBannerClicked,
            actionButtonText = Text.Resource(R.string.browser_menu_customize_banner_button),
            onDismissEvent = CustomizeMenuBannerDismissed,
        )

    override val itemsFlow: StateFlow<List<MenuItem>> = items.asStateFlow()

    override fun handles(event: MenuEvent) =
        event == CustomizeMenuBannerClicked || event == CustomizeMenuBannerDismissed

    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        if (event == CustomizeMenuBannerClicked) {
            menu.navigate(MenuFragmentDirections.actionMenuFragmentToCustomizeMenuFragment())
        } else {
            settings.shouldShowCustomizeMenuBanner = false
            items.value = emptyList()
        }
    }
}
