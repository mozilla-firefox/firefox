/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.configuration

import androidx.annotation.VisibleForTesting
import org.mozilla.fenix.R
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications
import org.mozilla.fenix.components.menu.CustomizedMenuInfo
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.AddToHomeScreen
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Back
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Bookmark
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Bookmarks
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.CustomizeReaderView
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.DesktopSite
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Downloads
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Extensions
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.FindInPage
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Forward
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.History
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.IPProtection
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.More
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.MoveToNormalTabs
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.MozillaAccount
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.NewPrivateTab
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.NewTab
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.OpenInApp
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Passwords
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Print
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Quit
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Refresh
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.ReportBrokenSite
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.SaveAsPdf
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Settings
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Share
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Shortcut
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.SummarizePage
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.TabGroup
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Translate
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Grid
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Row

/** The sections of the menu shown while browsing. */
object BrowserMenuConfiguration {
    @VisibleForTesting internal const val PAGE_CONTROLS_GROUP_ID = "page_controls"
    @VisibleForTesting internal const val READER_VIEW_GROUP_ID = "reader_view"
    @VisibleForTesting internal const val VPN_GROUP_ID = "vpn"
    @VisibleForTesting internal const val TOOLS_GROUP_ID = "tools"
    @VisibleForTesting internal const val LIBRARY_GROUP_ID = "library"
    @VisibleForTesting internal const val ACCOUNTS_AND_SETTINGS_GROUP_ID = "accounts_and_settings"
    @VisibleForTesting internal const val QUIT_GROUP_ID = "quit"
    @VisibleForTesting internal const val TABS_CONTROLS_GROUP_ID = "tabs_controls"

    /** Ids of the sections of the browser menu which the user cannot remove. */
    val REQUIRED_SECTION_IDS = setOf(ACCOUNTS_AND_SETTINGS_GROUP_ID)

    /** Ids of browser menu sections that start as initially hidden. */
    val HIDDEN_BY_DEFAULT_SECTION_IDS = setOf(TABS_CONTROLS_GROUP_ID)

    /**
     * The menu shown while browsing.
     *
     * @param isToolbarAtBottom Whether the toolbar is shown at the bottom of the screen.
     * @param isExpandedToolbarEnabled Whether the expanded toolbar is used.
     * @param customizationInfo The order and visibility of the sections chosen by the user. The default sections are
     *   shown if this is `null`.
     */
    fun sections(
        isToolbarAtBottom: Boolean,
        isExpandedToolbarEnabled: Boolean,
        customizationInfo: CustomizedMenuInfo? = null,
    ): List<MenuSectionConfiguration> {
        val shouldShowNavigationItemsAtTheTop =
            shouldShowNavigationItemsAtTheTop(isToolbarAtBottom, isExpandedToolbarEnabled)
        val defaultSections = allSections(shouldShowNavigationItemsAtTheTop)
        if (customizationInfo == null) {
            return defaultSections.filterNot { it.id in HIDDEN_BY_DEFAULT_SECTION_IDS }
        }

        val defaultSectionsIds = defaultSections.associateBy { it.id }
        val visibleSectionIds =
            if (customizationInfo.isNavigationSectionMoved) {
                customizationInfo.visibleSectionIds
            } else {
                customizationInfo.visibleSectionIds.withNavigationSectionOnToolbarSide(
                    shouldShowNavigationItemsAtTheTop
                )
            }
        return visibleSectionIds.mapNotNull { defaultSectionsIds[it] }
    }

    /**
     * Whether the navigation section is shown away from the toolbar's side of the menu, indicating the user moved it
     * there.
     *
     * @param visibleSectionIds Ids of the sections to show, in the order they should be shown in.
     * @param isToolbarAtBottom Whether the toolbar is shown at the bottom of the screen.
     * @param isExpandedToolbarEnabled Whether the expanded toolbar is used.
     */
    fun isNavigationSectionMoved(
        visibleSectionIds: List<String>,
        isToolbarAtBottom: Boolean,
        isExpandedToolbarEnabled: Boolean,
    ): Boolean {
        val index = visibleSectionIds.indexOf(PAGE_CONTROLS_GROUP_ID)
        val toolbarSideIndex =
            when (shouldShowNavigationItemsAtTheTop(isToolbarAtBottom, isExpandedToolbarEnabled)) {
                true -> 0
                else -> visibleSectionIds.lastIndex
            }
        return index != -1 && index != toolbarSideIndex
    }

    /** The sections of the browser menu the user removed, in their stored order. */
    fun hiddenSections(customization: CustomizedMenuInfo): List<MenuSectionConfiguration> {
        val sections = allSections().associateBy { it.id }
        return customization.hiddenSectionIds.mapNotNull { sections[it] }
    }

    /**
     * The structure of the browser menu for users who didn't customize it.
     *
     * @param isToolbarAtBottom Whether the toolbar is shown at the bottom of the screen.
     * @param isExpandedToolbarEnabled Whether the expanded toolbar is used.
     */
    fun defaultMenuInfo(isToolbarAtBottom: Boolean, isExpandedToolbarEnabled: Boolean): CustomizedMenuInfo {
        val (hiddenIds, shownIds) =
            allSections(shouldShowNavigationItemsAtTheTop(isToolbarAtBottom, isExpandedToolbarEnabled))
                .map { it.id }
                .partition { it in HIDDEN_BY_DEFAULT_SECTION_IDS }
        return CustomizedMenuInfo(visibleSectionIds = shownIds, hiddenSectionIds = hiddenIds)
    }

    /** All the sections of the browser menu, including the ones hidden by default. */
    fun allSections(): List<MenuSectionConfiguration> = allSections(shouldShowNavigationItemsAtTheTop = true)

    private fun shouldShowNavigationItemsAtTheTop(isToolbarAtBottom: Boolean, isExpandedToolbarEnabled: Boolean) =
        !isToolbarAtBottom && !isExpandedToolbarEnabled

    /** All the sections of the browser menu, including the ones hidden by default, in their default order. */
    private fun allSections(shouldShowNavigationItemsAtTheTop: Boolean): List<MenuSectionConfiguration> {
        val navSection =
            MenuSectionConfiguration(
                id = PAGE_CONTROLS_GROUP_ID,
                presentationMode = Grid,
                items = listOf(Back, Forward, Share, Refresh),
                isSticky = true,
                name = R.string.browser_menu_section_page_controls,
            )
        val rest =
            listOf(
                MenuSectionConfiguration(
                    id = READER_VIEW_GROUP_ID,
                    presentationMode = Row,
                    items = listOf(CustomizeReaderView),
                ),
                MenuSectionConfiguration(
                    id = VPN_GROUP_ID,
                    presentationMode = Row,
                    items = listOf(IPProtection),
                ),
                MenuSectionConfiguration(
                    id = TOOLS_GROUP_ID,
                    presentationMode = Row,
                    items =
                        listOf(
                            Bookmark,
                            FindInPage,
                            DesktopSite,
                            Extensions,
                            More(
                                listOf(
                                    Translate,
                                    SummarizePage,
                                    MoveToNormalTabs,
                                    ReportBrokenSite,
                                    Shortcut,
                                    AddToHomeScreen,
                                    TabGroup,
                                    OpenInApp,
                                    SaveAsPdf,
                                    Print,
                                )
                            ),
                        ),
                    name = R.string.browser_menu_section_tools,
                ),
                MenuSectionConfiguration(
                    id = LIBRARY_GROUP_ID,
                    presentationMode = Grid,
                    items = listOf(History, Bookmarks, Downloads, Passwords),
                    name = R.string.browser_menu_section_library,
                ),
                MenuSectionConfiguration(
                    id = ACCOUNTS_AND_SETTINGS_GROUP_ID,
                    presentationMode = Row,
                    items = listOf(MozillaAccount, Settings),
                    name = R.string.browser_menu_section_accounts_and_settings,
                ),
                MenuSectionConfiguration(
                    id = QUIT_GROUP_ID,
                    presentationMode = Row,
                    items = listOf(Quit),
                ),
                MenuSectionConfiguration(
                    id = TABS_CONTROLS_GROUP_ID,
                    presentationMode = Row,
                    items = listOf(NewTab, NewPrivateTab),
                    name = R.string.browser_menu_section_new_tab,
                ),
            )
        return if (shouldShowNavigationItemsAtTheTop) listOf(navSection) + rest else rest + navSection
    }

    /** Move the navigation section, if shown, to the toolbar's side of the menu. */
    private fun List<String>.withNavigationSectionOnToolbarSide(
        shouldShowNavigationItemsAtTheTop: Boolean
    ): List<String> {
        if (PAGE_CONTROLS_GROUP_ID !in this) return this

        val rest = this - PAGE_CONTROLS_GROUP_ID
        return when (shouldShowNavigationItemsAtTheTop) {
            true -> listOf(PAGE_CONTROLS_GROUP_ID) + rest
            else -> rest + PAGE_CONTROLS_GROUP_ID
        }
    }

    /**
     * Whether any of [notifications] needs the user's attention on an item shown in the browser menu, so the menu
     * button should be highlighted.
     */
    fun hasNotificationToShow(
        notifications: Collection<SupportedMenuNotifications>,
        customizationInfo: CustomizedMenuInfo?,
    ): Boolean = notifications.any { isShown(it, customizationInfo) }

    private fun isShown(notification: SupportedMenuNotifications, customizationInfo: CustomizedMenuInfo?): Boolean {
        val item = notification.menuItem ?: return false
        val section = allSections().find { it.containsItem(item) } ?: return false
        return when (customizationInfo) {
            null -> section.id !in HIDDEN_BY_DEFAULT_SECTION_IDS
            else -> section.id in customizationInfo.visibleSectionIds
        }
    }

    private val SupportedMenuNotifications.menuItem: FenixMenuItem?
        get() =
            when (this) {
                is SupportedMenuNotifications.Downloads -> Downloads
                is SupportedMenuNotifications.OpenInApp -> OpenInApp
                is SupportedMenuNotifications.Summarize -> SummarizePage
                is SupportedMenuNotifications.ExtensionUpdate -> Extensions
                is SupportedMenuNotifications.NotDefaultBrowser,
                is SupportedMenuNotifications.TabGroups -> null
            }

    private fun MenuSectionConfiguration.containsItem(targetItem: FenixMenuItem): Boolean = items.any { item ->
        item == targetItem || (item is FenixExpandableMenuItem && targetItem in item.subMenuItems)
    }
}
