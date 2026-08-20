/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites.interactor

import mozilla.components.feature.top.sites.TopSite
import org.mozilla.fenix.home.topsites.AddShortcutEntryPoint
import org.mozilla.fenix.home.topsites.AddShortcutSource
import org.mozilla.fenix.home.topsites.controller.TopSiteController

/** Interface for top site related actions on the homepage. */
interface TopSiteInteractor {
    /**
     * Opens the given top site in private mode. Called when an user clicks on the "Open in private tab" top site menu
     * item.
     *
     * @param topSite The top site that will be open in private mode.
     */
    fun onOpenInPrivateTabClicked(topSite: TopSite)

    /**
     * Opens a dialog to edit the given top site. Called when an user clicks on the "Edit" top site menu item.
     *
     * @param topSite The top site that will be edited.
     */
    fun onEditTopSiteClicked(topSite: TopSite)

    /**
     * Removes the given top site. Called when an user clicks on the "Remove" top site menu item.
     *
     * @param topSite The top site that will be removed.
     */
    fun onRemoveTopSiteClicked(topSite: TopSite)

    /**
     * Selects the given top site. Called when a user clicks on a top site.
     *
     * @param topSite The top site that was selected.
     * @param position The position of the top site.
     */
    fun onSelectTopSite(topSite: TopSite, position: Int)

    /**
     * Called when a user sees a provided top site.
     *
     * @param topSite The provided top site that was seen by the user.
     * @param position The position of the top site.
     */
    fun onTopSiteImpression(topSite: TopSite.Provided, position: Int)

    /** Navigates to the Homepage Settings. Called when an user clicks on the "Settings" top site menu item. */
    fun onSettingsClicked()

    /**
     * Opens the sponsor privacy support articles. Called when an user clicks on the "Our sponsors & your privacy" top
     * site menu item.
     */
    fun onSponsorPrivacyClicked()

    /**
     * Handles long click event for the given top site. Called when an user long clicks on a top site.
     *
     * @param topSite The top site that was long clicked.
     */
    fun onTopSiteLongClicked(topSite: TopSite)

    /**
     * Navigates to the Shortcuts screen. Called when an user clicks on the "Show all" button for shortcuts on the
     * homepage.
     */
    fun onShowAllTopSitesClicked()

    /**
     * Records telemetry when the user expands or collapses the shortcuts section. Called when an user clicks on the
     * expand/collapse control.
     *
     * @param isExpanded Whether the section is now expanded (true) or collapsed (false).
     */
    fun onExpandToggleClicked(isExpanded: Boolean)

    /** Sends telemetry related to the shortcuts library being viewed. */
    fun onShortcutsLibraryViewed()

    /**
     * Saves a new shortcut. Called when the user confirms adding a website from the "Add shortcut" dialog.
     *
     * @param title The title of the new shortcut.
     * @param url The URL of the new shortcut.
     * @param source The [AddShortcutSource] of how the shortcut was added.
     * @param entryPoint The [AddShortcutEntryPoint] of where the add flow was started from.
     */
    fun onSaveShortcut(
        title: String,
        url: String,
        source: AddShortcutSource,
        entryPoint: AddShortcutEntryPoint,
    )
}

/** Default implementation of [TopSiteInteractor]. */
class DefaultTopSiteInteractor(private val controller: TopSiteController) : TopSiteInteractor {
    override fun onOpenInPrivateTabClicked(topSite: TopSite) {
        controller.handleOpenInPrivateTabClicked(topSite)
    }

    override fun onEditTopSiteClicked(topSite: TopSite) {
        controller.handleEditTopSiteClicked(topSite)
    }

    override fun onRemoveTopSiteClicked(topSite: TopSite) {
        controller.handleRemoveTopSiteClicked(topSite)
    }

    override fun onSelectTopSite(
        topSite: TopSite,
        position: Int,
    ) {
        controller.handleSelectTopSite(topSite, position)
    }

    override fun onTopSiteImpression(
        topSite: TopSite.Provided,
        position: Int,
    ) {
        controller.handleTopSiteImpression(topSite, position)
    }

    override fun onSettingsClicked() {
        controller.handleTopSiteSettingsClicked()
    }

    override fun onSponsorPrivacyClicked() {
        controller.handleSponsorPrivacyClicked()
    }

    override fun onTopSiteLongClicked(topSite: TopSite) {
        controller.handleTopSiteLongClicked(topSite)
    }

    override fun onShowAllTopSitesClicked() {
        controller.handleShowAllTopSitesClicked()
    }

    override fun onExpandToggleClicked(isExpanded: Boolean) {
        controller.handleExpandToggleClicked(isExpanded)
    }

    override fun onSaveShortcut(
        title: String,
        url: String,
        source: AddShortcutSource,
        entryPoint: AddShortcutEntryPoint,
    ) {
        controller.handleSaveShortcut(
            title = title,
            url = url,
            source = source,
            entryPoint = entryPoint,
        )
    }

    override fun onShortcutsLibraryViewed() {
        controller.handleShortcutsLibraryViewed()
    }
}
