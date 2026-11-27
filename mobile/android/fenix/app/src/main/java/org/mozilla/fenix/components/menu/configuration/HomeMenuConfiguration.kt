/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.configuration

import androidx.annotation.VisibleForTesting
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications
import org.mozilla.fenix.components.menu.CustomizedMenuInfo
import org.mozilla.fenix.components.menu.CustomizedSectionInfo
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Bookmarks
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.CustomizeHomepage
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.DefaultBrowserBanner
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Downloads
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Extensions
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.History
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.IPProtection
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.MozillaAccount
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Passwords
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Quit
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Settings
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Grid
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Row

/** The sections of the menu shown on the home screen. */
object HomeMenuConfiguration {
    @VisibleForTesting internal val PROMO_BANNER_GROUP_ID = "promo_banner"
    @VisibleForTesting internal val VPN_GROUP_ID = "vpn"
    @VisibleForTesting internal val EXTENSIONS_GROUP_ID = "extensions"
    @VisibleForTesting internal val LIBRARY_GROUP_ID = "library"
    @VisibleForTesting internal val ACCOUNTS_AND_SETTINGS_GROUP_ID = "accounts_and_settings"
    @VisibleForTesting internal val QUIT_GROUP_ID = "quit"

    private val browserSectionIds: Map<String, String> =
        mapOf(
            VPN_GROUP_ID to BrowserMenuConfiguration.VPN_GROUP_ID,
            // Extensions sits in the Tools section of the browser menu.
            EXTENSIONS_GROUP_ID to BrowserMenuConfiguration.TOOLS_GROUP_ID,
            LIBRARY_GROUP_ID to BrowserMenuConfiguration.LIBRARY_GROUP_ID,
            ACCOUNTS_AND_SETTINGS_GROUP_ID to BrowserMenuConfiguration.ACCOUNTS_AND_SETTINGS_GROUP_ID,
            QUIT_GROUP_ID to BrowserMenuConfiguration.QUIT_GROUP_ID,
        )

    /**
     * The menu shown on the home screen.
     *
     * The promo banner is not part of the browser menu, so it always stays first.
     *
     * @param customizationInfo The order and visibility of the browser menu sections chosen by the user. The default
     *   sections are shown if this is `null`.
     * @param customizedSectionsInfo The items of each browser menu section chosen by the user, keyed by section id. The
     *   Extensions section is left out when Extensions is not shown in the browser Tools section.
     */
    fun sections(
        customizationInfo: CustomizedMenuInfo? = null,
        customizedSectionsInfo: Map<String, CustomizedSectionInfo> = emptyMap(),
    ): List<MenuSectionConfiguration> {
        val shownSections =
            defaultSections().filterNot {
                it.id == EXTENSIONS_GROUP_ID && !isExtensionsShownInBrowserMenu(customizedSectionsInfo)
            }
        if (customizationInfo == null) return shownSections

        val (promoBanner, following) = shownSections.partition { it.id == PROMO_BANNER_GROUP_ID }
        val followingByBrowserId = following.associateBy { browserSectionIds.getValue(it.id) }
        return promoBanner + customizationInfo.visibleSectionIds.mapNotNull { followingByBrowserId[it] }
    }

    /**
     * Whether any of [notifications] needs the user's attention on an item the home menu shows, so the menu button
     * should be highlighted.
     *
     * @param notifications The notifications to check.
     * @param customizationInfo The order and visibility of the browser menu sections chosen by the user, or `null` for
     *   the default.
     * @param customizedSectionsInfo The items of each browser menu section chosen by the user, keyed by section id. The
     *   Extensions section is left out when Extensions is not shown in the browser Tools section.
     */
    fun hasNotificationToShow(
        notifications: Collection<SupportedMenuNotifications>,
        customizationInfo: CustomizedMenuInfo?,
        customizedSectionsInfo: Map<String, CustomizedSectionInfo> = emptyMap(),
    ): Boolean {
        val notificationItems = notifications.mapNotNull { it.menuItem }
        if (notificationItems.isEmpty()) return false

        val shownItems = sections(customizationInfo, customizedSectionsInfo).flatMap { it.items }
        return notificationItems.any { it in shownItems }
    }

    /** Whether Extensions is shown in the Tools section of the browser menu. */
    private fun isExtensionsShownInBrowserMenu(customizedSectionsInfo: Map<String, CustomizedSectionInfo>): Boolean {
        val toolsInfo = customizedSectionsInfo[BrowserMenuConfiguration.TOOLS_GROUP_ID] ?: return true
        return toolsInfo.items.any { Extensions.id == it.id || Extensions.id in it.subItemIds }
    }

    private val SupportedMenuNotifications.menuItem: FenixMenuItem?
        get() =
            when (this) {
                is SupportedMenuNotifications.NotDefaultBrowser -> DefaultBrowserBanner
                is SupportedMenuNotifications.Downloads -> Downloads
                is SupportedMenuNotifications.ExtensionUpdate -> Extensions
                is SupportedMenuNotifications.Summarize,
                is SupportedMenuNotifications.TabGroups,
                is SupportedMenuNotifications.OpenInApp -> null
            }

    private fun defaultSections(): List<MenuSectionConfiguration> =
        listOf(
            MenuSectionConfiguration(
                id = PROMO_BANNER_GROUP_ID,
                presentationMode = Row,
                items = listOf(DefaultBrowserBanner),
            ),
            MenuSectionConfiguration(
                id = VPN_GROUP_ID,
                presentationMode = Row,
                items = listOf(IPProtection),
            ),
            MenuSectionConfiguration(
                id = EXTENSIONS_GROUP_ID,
                presentationMode = Row,
                items = listOf(Extensions),
            ),
            MenuSectionConfiguration(
                id = LIBRARY_GROUP_ID,
                presentationMode = Grid,
                items = listOf(History, Bookmarks, Downloads, Passwords),
            ),
            MenuSectionConfiguration(
                id = ACCOUNTS_AND_SETTINGS_GROUP_ID,
                presentationMode = Row,
                items = listOf(MozillaAccount, CustomizeHomepage, Settings),
            ),
            MenuSectionConfiguration(
                id = QUIT_GROUP_ID,
                presentationMode = Row,
                items = listOf(Quit),
            ),
        )
}
