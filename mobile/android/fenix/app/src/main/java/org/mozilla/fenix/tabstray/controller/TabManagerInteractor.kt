/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.controller

import mozilla.components.browser.storage.sync.Tab
import org.mozilla.fenix.tabstray.SyncedTabsInteractor
import org.mozilla.fenix.tabstray.browser.InactiveTabsInteractor
import org.mozilla.fenix.tabstray.browser.TabsTrayFabInteractor
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.redux.state.Page

/** Interactor for responding to all user actions in the tab manager. */
interface TabManagerInteractor : SyncedTabsInteractor, InactiveTabsInteractor, TabsTrayFabInteractor {

    /**
     * Invoked when a page in the tab manager is clicked.
     *
     * @param page The page on the tab manager to focus.
     */
    fun onTabPageClicked(page: Page)

    /**
     * Invoked when the user confirmed tab removal that would lead to cancelled private downloads.
     *
     * @param tabId ID of the tab being removed.
     * @param source is the app feature from which the [TabsTrayItem] with [tabId] was closed.
     */
    fun onDeletePrivateTabWarningAccepted(tabId: String, source: String? = null)

    /** Invoked when the selected tabs are requested to be deleted. */
    fun onDeleteSelectedTabsClicked()

    /** Invoked when the debug menu option for inactive tabs is clicked. */
    fun onForceSelectedTabsAsInactiveClicked()

    /** Invoked when the bookmark button in the multi selection banner is clicked. */
    fun onBookmarkSelectedTabsClicked()

    /** Invoked when the share button in the multi selection banner is clicked. */
    fun onShareSelectedTabs()

    /** Invoked when the recently closed item is clicked. */
    fun onRecentlyClosedClicked()

    /**
     * Invoked when the back button is pressed.
     *
     * @return true if the back button press was consumed.
     */
    fun onBackPressed(): Boolean

    /** Invoked when the sign into sync button is clicked. */
    fun onSignInClicked()

    /** A new tab has been selected. */
    fun onTabSelected(tab: TabsTrayItem.Tab, source: String? = null)

    /** A tab has been closed. */
    fun onTabClosed(tab: TabsTrayItem.Tab, source: String? = null)
}

/**
 * Default implementation of [TabManagerInteractor].
 *
 * @param controller [TabManagerController] to which user actions can be delegated for app updates.
 */
class DefaultTabManagerInteractor(private val controller: TabManagerController) : TabManagerInteractor {

    override fun onTabPageClicked(page: Page) {
        controller.handleTabPageClicked(page)
    }

    override fun onDeletePrivateTabWarningAccepted(tabId: String, source: String?) {
        controller.handleDeletePrivateTabWarningAccepted(tabId = tabId, source = source)
    }

    override fun onDeleteSelectedTabsClicked() {
        controller.handleDeleteSelectedTabsClicked()
    }

    override fun onForceSelectedTabsAsInactiveClicked() {
        controller.handleForceSelectedTabsAsInactiveClicked()
    }

    override fun onBookmarkSelectedTabsClicked() {
        controller.handleBookmarkSelectedTabsClicked()
    }

    override fun onShareSelectedTabs() {
        controller.handleShareSelectedTabsClicked()
    }

    override fun onSyncedTabClicked(tab: Tab) {
        controller.handleSyncedTabClicked(tab)
    }

    override fun onSyncedTabClosed(deviceId: String, tab: Tab) {
        controller.handleSyncedTabClosed(deviceId, tab)
    }

    override fun onBackPressed(): Boolean = controller.handleBackPressed()

    override fun onTabClosed(tab: TabsTrayItem.Tab, source: String?) {
        controller.handleTabDeletion(tab, source)
    }

    override fun onTabSelected(tab: TabsTrayItem.Tab, source: String?) {
        controller.handleTabSelected(tab, source)
    }

    override fun onNormalTabsFabClicked() {
        controller.handleNormalTabsFabClick()
    }

    override fun onPrivateTabsFabClicked() {
        controller.handlePrivateTabsFabClick()
    }

    override fun onSyncedTabsFabClicked() {
        controller.handleSyncedTabsFabClick()
    }

    override fun onRecentlyClosedClicked() {
        controller.handleNavigateToRecentlyClosed()
    }

    /** See [InactiveTabsInteractor.onInactiveTabsHeaderClicked]. */
    override fun onInactiveTabsHeaderClicked(expanded: Boolean) {
        controller.handleInactiveTabsHeaderClicked(expanded)
    }

    /** See [InactiveTabsInteractor.onAutoCloseDialogCloseButtonClicked]. */
    override fun onAutoCloseDialogCloseButtonClicked() {
        controller.handleInactiveTabsAutoCloseDialogDismiss()
    }

    /** See [InactiveTabsInteractor.onEnableAutoCloseClicked]. */
    override fun onEnableAutoCloseClicked() {
        controller.handleEnableInactiveTabsAutoCloseClicked()
    }

    /** See [InactiveTabsInteractor.onInactiveTabClicked]. */
    override fun onInactiveTabClicked(tab: TabsTrayItem.Tab) {
        controller.handleInactiveTabClicked(tab)
    }

    /** See [InactiveTabsInteractor.onInactiveTabClosed]. */
    override fun onInactiveTabClosed(tab: TabsTrayItem.Tab) {
        controller.handleCloseInactiveTabClicked(tab)
    }

    /** See [InactiveTabsInteractor.onDeleteAllInactiveTabsClicked]. */
    override fun onDeleteAllInactiveTabsClicked() {
        controller.handleDeleteAllInactiveTabsClicked()
    }

    override fun onSignInClicked() {
        controller.handleSignInClicked()
    }
}
