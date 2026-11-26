/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.configuration

/** Type of items that can be expanded to show other (even unrelated) menu items. */
sealed interface FenixExpandableMenuItem {

    /** The other menu items to show when this is expanded. */
    val subMenuItems: List<FenixMenuItem>
}

/**
 * All items that can be shown in the menu.
 *
 * @property id Unique identifier of this item. Persisted when the user customizes the menu, so it must never change
 *   once shipped.
 * @property isTopLevelOnly Whether this item can only be directly in its section, never inside another item.
 */
sealed class FenixMenuItem(val id: String, val isTopLevelOnly: Boolean = false) {
    /** A banner allowing to set this application as the default browser. */
    data object DefaultBrowserBanner : FenixMenuItem("default_browser_banner")

    /** A menu item allowing to customize the reader view. */
    data object CustomizeReaderView : FenixMenuItem("customize_reader_view")

    /** A menu item allowing to view the current VPN status or change its configuration. */
    data object IPProtection : FenixMenuItem("ip_protection")

    /** A menu item allowing to bookmark the current page, or to edit the bookmark it already has. */
    data object Bookmark : FenixMenuItem("bookmark")

    /** A menu item expanding to the user's extensions, or to a list of recommended ones. */
    data object Extensions : FenixMenuItem("extensions", isTopLevelOnly = true)

    /** A menu item allowing to start the find in page feature. */
    data object FindInPage : FenixMenuItem("find_in_page")

    /** A menu item allowing to switch the current page between the desktop and the mobile version. */
    data object DesktopSite : FenixMenuItem("desktop_site")

    /**
     * A menu item expanding to show more general menu items related to the current webpage. *
     *
     * @property subMenuItems The other menu items to show when this is expanded.
     */
    data class More(override val subMenuItems: List<FenixMenuItem>) : FenixMenuItem("more"), FenixExpandableMenuItem

    /** A menu item allowing to translate the current page. */
    data object Translate : FenixMenuItem("translate")

    /** A menu item allowing to summarize the current page. */
    data object SummarizePage : FenixMenuItem("summarize_page")

    /** A menu item allowing to move the current private tab to a non-private tab. */
    data object MoveToNormalTabs : FenixMenuItem("move_to_normal_tabs")

    /** A menu item allowing to report the current page as broken. */
    data object ReportBrokenSite : FenixMenuItem("report_broken_site")

    /** A menu item allowing to add or remove the current webpage from home shortcuts. */
    data object Shortcut : FenixMenuItem("shortcut")

    /** A menu item allowing to add the current webpage as a shortcut on the device's home screen. */
    data object AddToHomeScreen : FenixMenuItem("add_to_home_screen")

    /** A menu item allowing to open the current webpage in the app that handles it. */
    data object OpenInApp : FenixMenuItem("open_in_app")

    /** A menu item allowing to open the current custom tab in the main application. */
    data object OpenInFirefox : FenixMenuItem("open_in_firefox")

    /** The menu items added by the app that opened the current custom tab. */
    data object CustomTabAppItems : FenixMenuItem("custom_tab_app_items")

    /** A menu item allowing to save the current webpage as a PDF. */
    data object SaveAsPdf : FenixMenuItem("save_as_pdf")

    /** A menu item allowing to print the current webpage. */
    data object Print : FenixMenuItem("print")

    /** A menu item allowing to sign in or manage the current Mozilla account. */
    data object MozillaAccount : FenixMenuItem("mozilla_account")

    /** A menu item allowing to customize what the homepage shows. */
    data object CustomizeHomepage : FenixMenuItem("customize_homepage")

    /** A menu item allowing to open the application settings. */
    data object Settings : FenixMenuItem("settings")

    /** A menu item allowing to delete the browsing data of this session and quit the application. */
    data object Quit : FenixMenuItem("quit")

    /** A menu item allowing to navigate back. */
    data object Back : FenixMenuItem("back")

    /** A menu item allowing to open the history screen. */
    data object History : FenixMenuItem("history")

    /** A menu item allowing to open the bookmarks screen. */
    data object Bookmarks : FenixMenuItem("bookmarks")

    /** A menu item allowing to open the downloads screen. */
    data object Downloads : FenixMenuItem("downloads")

    /** A menu item allowing to open the passwords screen. */
    data object Passwords : FenixMenuItem("passwords")

    /** A menu item allowing to navigate forward. */
    data object Forward : FenixMenuItem("forward")

    /** A menu item allowing to share the current page. */
    data object Share : FenixMenuItem("share")

    /** A menu item allowing to refresh or stop loading the current page. */
    data object Refresh : FenixMenuItem("refresh")

    /** A menu item allowing to open a new tab in normal mode. */
    data object NewTab : FenixMenuItem("new_tab")

    /** A menu item allowing to open a new tab in private mode. */
    data object NewPrivateTab : FenixMenuItem("new_private_tab")

    /** A menu item allowing users to add the current tab to a tab group, or move it to another one. */
    data object TabGroup : FenixMenuItem("tab_group")
}
