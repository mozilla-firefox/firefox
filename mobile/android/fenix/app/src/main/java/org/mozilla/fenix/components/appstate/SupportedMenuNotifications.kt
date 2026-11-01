/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.appstate

/** Represents the different types of notifications that can be displayed in the menu. */
sealed class SupportedMenuNotifications {
    /** Represents a notification related to downloads. This is used to indicate completed downloads. */
    object Downloads : SupportedMenuNotifications()

    /**
     * Represents a notification related to open in app. This is used to indicate the link can be opened in an external
     * app.
     */
    object OpenInApp : SupportedMenuNotifications()

    /**
     * Represents a notification related to the main menu button (the hamburger menu). The menu button is highlighted
     * after onboarding when the app is not the default browser.
     */
    object NotDefaultBrowser : SupportedMenuNotifications()

    /**
     * Represents a notification related to Page summarization.
     *
     * This is currently used for enabling users to discover the feature
     */
    data object Summarize : SupportedMenuNotifications()

    /** An extension update is waiting for the user to approve its new permissions. */
    data object ExtensionUpdate : SupportedMenuNotifications()
}
