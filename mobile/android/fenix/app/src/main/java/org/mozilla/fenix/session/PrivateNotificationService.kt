/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.session

import android.annotation.SuppressLint
import android.content.Intent
import android.os.Build
import androidx.annotation.VisibleForTesting
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import java.util.Locale
import mozilla.components.browser.state.selector.normalTabs
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.feature.privatemode.notification.AbstractPrivateNotificationService
import mozilla.components.support.base.android.NotificationsDelegate
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.IntentReceiverActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.customtabs.ExternalAppBrowserActivity
import org.mozilla.fenix.ext.components

/**
 * Manages notifications for private tabs.
 *
 * Private tab notifications solve two problems for us: 1 - They allow users to interact with us from outside of the app
 * (example: by closing all private tabs). 2 - The notification will keep our process alive, allowing us to keep private
 * tabs in memory.
 *
 * As long as a session is active this service will keep its notification alive.
 */
class PrivateNotificationService : AbstractPrivateNotificationService() {

    override val crashReporter: CrashReporting by lazy { components.analytics.crashReporter }

    override val store: BrowserStore by lazy { components.core.store }

    override val notificationsDelegate: NotificationsDelegate by lazy { components.notificationsDelegate }

    /**
     * Finishes and removes the app's tasks if the app is in the background. Returns true if it was in the background.
     */
    @VisibleForTesting
    internal var finishAndRemoveTaskIfInBackground: () -> Boolean = {
        VisibilityLifecycleCallback.finishAndRemoveTaskIfInBackground(this)
    }

    override fun NotificationCompat.Builder.buildNotification() {
        setSmallIcon(iconsR.drawable.mozac_ic_private_mode_fill_24)

        val contentTitle =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                applicationContext.getString(R.string.notification_erase_title_android_14)
            } else {
                applicationContext.getString(R.string.app_name_private_4, getString(R.string.app_name))
            }

        val contentText =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                applicationContext.getString(R.string.notification_erase_text_android_14)
            } else {
                applicationContext.getString(R.string.notification_pbm_delete_text_2)
            }

        setContentTitle(contentTitle)
        setContentText(contentText)

        color =
            ContextCompat.getColor(
                this@PrivateNotificationService,
                R.color.pbm_notification_color,
            )
    }

    /** Update the existing notification when the [Locale] has been changed. */
    override fun notifyLocaleChanged() {
        super.refreshNotification()
    }

    @SuppressLint("MissingSuperCall")
    override fun erasePrivateTabs() {
        val inPrivateMode = store.state.selectedTab?.content?.private ?: false
        val isHomepageAsNewTabEnabled = components.settings.enableHomepageAsNewTab
        val appStore = components.appStore

        // Use inPrivateMode to short circuit and not run finishAndRemoveTaskIfInBackground when not in private mode
        val isInBackground = inPrivateMode && finishAndRemoveTaskIfInBackground()

        if (inPrivateMode && isHomepageAsNewTabEnabled) {
            if (isInBackground) {
                switchToNormalMode()
            } else {
                // Update tabs tray visibility before removing all tabs so that HomepageTabBinding doesn't add a
                // new tab.
                appStore.dispatch(AppAction.UpdateTabsTrayVisibility(true))
            }
        }

        // Trigger use case directly for now (instead of calling super.erasePrivateTabs)
        // as otherwise SessionManager and the store will be out of sync.
        components.useCases.tabsUseCases.removePrivateTabs()

        // When Homepage as a New Tab is disabled, if the app is in private mode we launch to the private mode home
        // screen as a confirmation that all private tabs have been deleted. If we don't do this the user
        // will end up on a new selected tab in normal mode which isn't desired.
        // If the app is in normal mode there's no reason to direct the user away to
        // private mode as all private tabs have been deleted.
        // When Homepage as a New Tab is enabled, show the private tabs tray instead of the private
        // homepage, or stay in the background in normal mode if the app is in the background.
        if (inPrivateMode && (!isHomepageAsNewTabEnabled || !isInBackground)) {
            startHomeActivityInPrivateMode(openToTabsTray = isHomepageAsNewTabEnabled, isInBackground = isInBackground)
        }
    }

    /**
     * Switches to normal mode and selects the last accessed normal tab, so that the next launch opens it instead of
     * creating a new private tab. Must be called before the private tabs are removed so the selection is kept.
     */
    private fun switchToNormalMode() {
        components.settings.lastKnownMode = BrowsingMode.Normal
        components.appStore.dispatch(AppAction.BrowsingModeManagerModeChanged(BrowsingMode.Normal))

        store.state.normalTabs
            .maxByOrNull { it.lastAccess }
            ?.let {
                components.useCases.tabsUseCases.selectTab(it.id)
            }
    }

    private fun startHomeActivityInPrivateMode(openToTabsTray: Boolean, isInBackground: Boolean) {
        val homeScreenIntent =
            Intent(this, HomeActivity::class.java).apply {
                // Reuse the running HomeActivity when opening the tabs tray. Recreating it would let the old tabs
                // tray reset the tabs tray visibility after the new one is shown.
                flags =
                    if (openToTabsTray) {
                        Intent.FLAG_ACTIVITY_NEW_TASK
                    } else {
                        Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
                    }
                putExtra(HomeActivity.PRIVATE_BROWSING_MODE, true)
                if (openToTabsTray) {
                    putExtra(HomeActivity.OPEN_TO_TABS_TRAY, true)
                }
                if (isInBackground) {
                    // Set start mode to be in background (recents screen)
                    putExtra(HomeActivity.START_IN_RECENTS_SCREEN, true)
                }
            }

        startActivity(homeScreenIntent)
    }

    override fun ignoreTaskComponentClasses(): List<String> =
        listOf(
            ExternalAppBrowserActivity::class.qualifiedName!!,
            IntentReceiverActivity::class.qualifiedName!!,
        )

    override fun ignoreTaskActions(): List<String> = listOf()
}
