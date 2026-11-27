/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.configuration

import androidx.annotation.VisibleForTesting
import org.mozilla.fenix.R
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications
import org.mozilla.fenix.components.menu.CustomizedItemInfo
import org.mozilla.fenix.components.menu.CustomizedMenuInfo
import org.mozilla.fenix.components.menu.CustomizedSectionInfo
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.AddToHomeScreen
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Back
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Bookmark
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Bookmarks
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.CustomizeMenuBanner
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.CustomizeReaderView
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.DefaultBrowserBanner
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
    @VisibleForTesting internal const val PROMO_BANNER_GROUP_ID = "promo_banner"
    @VisibleForTesting internal const val READER_VIEW_GROUP_ID = "reader_view"
    internal const val VPN_GROUP_ID = "vpn"
    internal const val TOOLS_GROUP_ID = "tools"
    internal const val LIBRARY_GROUP_ID = "library"
    internal const val ACCOUNTS_AND_SETTINGS_GROUP_ID = "accounts_and_settings"
    internal const val QUIT_GROUP_ID = "quit"
    @VisibleForTesting internal const val TABS_CONTROLS_GROUP_ID = "tabs_controls"

    /** Ids of the sections of the browser menu which the user cannot remove. */
    val REQUIRED_SECTION_IDS = setOf(ACCOUNTS_AND_SETTINGS_GROUP_ID)

    /** Ids of browser menu sections that start as initially hidden. */
    val HIDDEN_BY_DEFAULT_SECTION_IDS = setOf(TABS_CONTROLS_GROUP_ID)

    /** Ids of the browser menu sections whose items the user can customize. */
    val ITEMS_CUSTOMIZABLE_SECTION_IDS = setOf(TOOLS_GROUP_ID)

    /**
     * The menu shown while browsing: the customizable sections, with the promo banner as the first scrolling section.
     *
     * @param isToolbarAtBottom Whether the toolbar is shown at the bottom of the screen.
     * @param isExpandedToolbarEnabled Whether the expanded toolbar is used.
     * @param customizationInfo The order and visibility of the sections chosen by the user. The default sections are
     *   shown if this is `null`.
     * @param customizedSectionsInfo The items of each section chosen by the user, keyed by section id. Sections not in
     *   here show their default items.
     */
    fun sections(
        isToolbarAtBottom: Boolean,
        isExpandedToolbarEnabled: Boolean,
        customizationInfo: CustomizedMenuInfo? = null,
        customizedSectionsInfo: Map<String, CustomizedSectionInfo> = emptyMap(),
    ): List<MenuSectionConfiguration> =
        customizableSections(isToolbarAtBottom, isExpandedToolbarEnabled, customizationInfo, customizedSectionsInfo)
            .withPromoBanner()

    /**
     * The sections of the menu shown while browsing which the user can move or hide.
     *
     * @param isToolbarAtBottom Whether the toolbar is shown at the bottom of the screen.
     * @param isExpandedToolbarEnabled Whether the expanded toolbar is used.
     * @param customizationInfo The order and visibility of the sections chosen by the user. The default sections are
     *   shown if this is `null`.
     * @param customizedSectionsInfo The items of each section chosen by the user, keyed by section id. Sections not in
     *   here show their default items.
     */
    fun customizableSections(
        isToolbarAtBottom: Boolean,
        isExpandedToolbarEnabled: Boolean,
        customizationInfo: CustomizedMenuInfo? = null,
        customizedSectionsInfo: Map<String, CustomizedSectionInfo> = emptyMap(),
    ): List<MenuSectionConfiguration> {
        val shouldShowNavigationItemsAtTheTop =
            shouldShowNavigationItemsAtTheTop(isToolbarAtBottom, isExpandedToolbarEnabled)
        val defaultSections =
            allSections(shouldShowNavigationItemsAtTheTop).map { it.withCustomizedItems(customizedSectionsInfo[it.id]) }
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
     * Whether the browser menu differs from its default, in the order and visibility of its sections or in the items of
     * any of them.
     *
     * @param customizationInfo The user's customization of the browser menu sections, `null` if they were never
     *   customized.
     * @param isToolbarAtBottom Whether the toolbar is shown at the bottom of the screen.
     * @param isExpandedToolbarEnabled Whether the expanded toolbar is used.
     * @param customizedSectionsInfo The items of each section chosen by the user, keyed by section id.
     */
    fun isCustomized(
        customizationInfo: CustomizedMenuInfo?,
        isToolbarAtBottom: Boolean,
        isExpandedToolbarEnabled: Boolean,
        customizedSectionsInfo: Map<String, CustomizedSectionInfo> = emptyMap(),
    ): Boolean =
        (customizationInfo != null &&
            areSectionsCustomized(customizationInfo, isToolbarAtBottom, isExpandedToolbarEnabled)) ||
            areItemsCustomized(customizedSectionsInfo)

    private fun areSectionsCustomized(
        customizationInfo: CustomizedMenuInfo,
        isToolbarAtBottom: Boolean,
        isExpandedToolbarEnabled: Boolean,
    ): Boolean {
        val default = defaultMenuInfo(isToolbarAtBottom, isExpandedToolbarEnabled)
        val shownSections = customizableSections(isToolbarAtBottom, isExpandedToolbarEnabled, customizationInfo)
        val defaultShownSections = customizableSections(isToolbarAtBottom, isExpandedToolbarEnabled)

        return shownSections.map { it.id } != defaultShownSections.map { it.id } ||
            customizationInfo.hiddenSectionIds.toSet() != default.hiddenSectionIds.toSet() ||
            customizationInfo.isNavigationSectionMoved
    }

    private fun areItemsCustomized(customizedSectionsInfo: Map<String, CustomizedSectionInfo>): Boolean {
        val defaults = defaultSectionsInfo()
        return customizedSectionsInfo.any { (sectionId, sectionInfo) -> sectionInfo != defaults[sectionId] }
    }

    private fun List<MenuSectionConfiguration>.withPromoBanner(): List<MenuSectionConfiguration> =
        listOf(
            MenuSectionConfiguration(
                id = PROMO_BANNER_GROUP_ID,
                presentationMode = Row,
                items = listOf(DefaultBrowserBanner, CustomizeMenuBanner),
                isFirstScrollingSection = true,
            )
        ) + this

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

    /**
     * The items of each section in [ITEMS_CUSTOMIZABLE_SECTION_IDS], keyed by section id, for users who didn't
     * customize them.
     */
    fun defaultSectionsInfo(): Map<String, CustomizedSectionInfo> =
        allSections()
            .filter { it.id in ITEMS_CUSTOMIZABLE_SECTION_IDS }
            .associate { section ->
                section.id to
                    CustomizedSectionInfo(
                        sectionId = section.id,
                        items = section.items.map { it.toCustomizedItemInfo() },
                        hiddenItems = emptyList(),
                    )
            }

    /**
     * Ids of the items of the browser menu, including the ones other items expand to, which can only be directly in
     * their section, never inside another item.
     */
    fun topLevelOnlyItemIds(): Set<String> =
        allSections()
            .flatMap { it.items }
            .flatMap { listOf(it) + it.expandsTo }
            .filter { it.isTopLevelOnly }
            .map { it.id }
            .toSet()

    /** All the sections of the browser menu, including the ones hidden by default. */
    fun allSections(): List<MenuSectionConfiguration> = allSections(shouldShowNavigationItemsAtTheTop = true)

    /**
     * All the sections of the browser menu, including the ones hidden by default, with the items chosen by the user.
     *
     * @param customizedSectionsInfo The items of each section chosen by the user, keyed by section id. Sections not in
     *   here have their default items.
     */
    fun allSections(customizedSectionsInfo: Map<String, CustomizedSectionInfo>): List<MenuSectionConfiguration> =
        allSections().map { it.withCustomizedItems(customizedSectionsInfo[it.id]) }

    private val FenixMenuItem.expandsTo: List<FenixMenuItem>
        get() = (this as? FenixExpandableMenuItem)?.subMenuItems.orEmpty()

    private fun FenixMenuItem.toCustomizedItemInfo() = CustomizedItemInfo(id = id, subItemIds = expandsTo.map { it.id })

    /** This section with the items chosen by the user in [sectionInfo], or with its default ones if that is `null`. */
    private fun MenuSectionConfiguration.withCustomizedItems(
        sectionInfo: CustomizedSectionInfo?
    ): MenuSectionConfiguration {
        if (sectionInfo == null) return this

        val available = items.flatMap { listOf(it) + it.expandsTo }.associateBy { it.id }
        return copy(
            items =
                sectionInfo.items.mapNotNull { item ->
                    when (val menuItem = available[item.id]) {
                        is More -> More(item.subItemIds.mapNotNull { available[it] })
                        else -> menuItem
                    }
                }
        )
    }

    private fun shouldShowNavigationItemsAtTheTop(isToolbarAtBottom: Boolean, isExpandedToolbarEnabled: Boolean) =
        !isToolbarAtBottom && !isExpandedToolbarEnabled

    /** All the sections of the browser menu, including the ones hidden by default, in their default order. */
    private fun allSections(shouldShowNavigationItemsAtTheTop: Boolean): List<MenuSectionConfiguration> {
        val navSection =
            MenuSectionConfiguration(
                id = PAGE_CONTROLS_GROUP_ID,
                presentationMode = Grid,
                items = listOf(Back, Forward, Bookmark, Share, Refresh),
                isSticky = true,
                name = R.string.browser_menu_section_page_controls,
                showTitles = false,
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
     *
     * @param notifications The notifications to check.
     * @param customizationInfo The order and visibility of the sections chosen by the user, or `null` for the default.
     * @param customizedSectionsInfo The items of each section chosen by the user, keyed by section id. Sections not in
     *   here show their default items.
     */
    fun hasNotificationToShow(
        notifications: Collection<SupportedMenuNotifications>,
        customizationInfo: CustomizedMenuInfo?,
        customizedSectionsInfo: Map<String, CustomizedSectionInfo> = emptyMap(),
    ): Boolean = notifications.any { isShown(it, customizationInfo, customizedSectionsInfo) }

    private fun isShown(
        notification: SupportedMenuNotifications,
        customizationInfo: CustomizedMenuInfo?,
        customizedSectionsInfo: Map<String, CustomizedSectionInfo>,
    ): Boolean {
        val item = notification.menuItem ?: return false
        val section = allSections(customizedSectionsInfo).find { it.containsItem(item) } ?: return false
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
