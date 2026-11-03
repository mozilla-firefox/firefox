/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.configuration

/** Type of items that can be expanded to show other (even unrelated) menu items. */
sealed interface FenixExpandableMenuItem {

    /** The other menu items to show when this is expanded. */
    val subMenuItems: List<FenixMenuItem>
}

/** All items that can be shown in the menu. */
sealed interface FenixMenuItem {
    /** A banner allowing to set this application as the default browser. */
    data object DefaultBrowserBanner : FenixMenuItem

    /** A menu item allowing to customize the reader view. */
    data object CustomizeReaderView : FenixMenuItem

    /** A menu item allowing to view the current VPN status or change its configuration. */
    data object IPProtection : FenixMenuItem

    /** A menu item allowing to bookmark the current page, or to edit the bookmark it already has. */
    data object Bookmark : FenixMenuItem

    /** A menu item expanding to the user's extensions, or to a list of recommended ones. */
    data object Extensions : FenixMenuItem

    /** A menu item allowing to start the find in page feature. */
    data object FindInPage : FenixMenuItem

    /** A menu item allowing to switch the current page between the desktop and the mobile version. */
    data object DesktopSite : FenixMenuItem

    /**
     * A menu item expanding to show more general menu items related to the current webpage. *
     *
     * @property subMenuItems The other menu items to show when this is expanded.
     */
    data class More(override val subMenuItems: List<FenixMenuItem>) : FenixMenuItem, FenixExpandableMenuItem

    /** A menu item allowing to translate the current page. */
    data object Translate : FenixMenuItem

    /** A menu item allowing to summarize the current page. */
    data object SummarizePage : FenixMenuItem

    /** A menu item allowing to move the current private tab to a non-private tab. */
    data object MoveToNormalTabs : FenixMenuItem

    /** A menu item allowing to report the current page as broken. */
    data object ReportBrokenSite : FenixMenuItem

    /** A menu item allowing to add or remove the current webpage from home shortcuts. */
    data object Shortcut : FenixMenuItem

    /** A menu item allowing to add the current webpage as a shortcut on the device's home screen. */
    data object AddToHomeScreen : FenixMenuItem

    /** A menu item allowing to add the current webpage to a collection. */
    data object SaveToCollection : FenixMenuItem

    /** A menu item allowing to open the current webpage in the app that handles it. */
    data object OpenInApp : FenixMenuItem

    /** A menu item allowing to open the current custom tab in the main application. */
    data object OpenInFirefox : FenixMenuItem

    /** The menu items added by the app that opened the current custom tab. */
    data object CustomTabAppItems : FenixMenuItem

    /** A menu item allowing to save the current webpage as a PDF. */
    data object SaveAsPdf : FenixMenuItem

    /** A menu item allowing to print the current webpage. */
    data object Print : FenixMenuItem

    /** A menu item allowing to sign in or manage the current Mozilla account. */
    data object MozillaAccount : FenixMenuItem

    /** A menu item allowing to customize what the homepage shows. */
    data object CustomizeHomepage : FenixMenuItem

    /** A menu item allowing to open the application settings. */
    data object Settings : FenixMenuItem

    /** A menu item allowing to delete the browsing data of this session and quit the application. */
    data object Quit : FenixMenuItem

    /** A menu item allowing to navigate back. */
    data object Back : FenixMenuItem

    /** A menu item allowing to open the history screen. */
    data object History : FenixMenuItem

    /** A menu item allowing to open the bookmarks screen. */
    data object Bookmarks : FenixMenuItem

    /** A menu item allowing to open the downloads screen. */
    data object Downloads : FenixMenuItem

    /** A menu item allowing to open the passwords screen. */
    data object Passwords : FenixMenuItem

    /** A menu item allowing to navigate forward. */
    data object Forward : FenixMenuItem

    /** A menu item allowing to share the current page. */
    data object Share : FenixMenuItem

    /** A menu item allowing to refresh or stop loading the current page. */
    data object Refresh : FenixMenuItem
}
