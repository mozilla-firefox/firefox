/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.intent

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.Settings
import androidx.navigation.NavController
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.BrowserDirection
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.GlobalDirections
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.ext.alreadyOnDestination
import org.mozilla.fenix.ext.openSetDefaultBrowserOption
import org.mozilla.fenix.utils.maybeShowAddSearchWidgetPrompt
import org.mozilla.fenix.utils.Settings as AppSettings

// Intent extra to enable or disable TabTray animation setting for testing
private const val EXTRA_TAB_TRAY_ANIMATION = "EXTRA_TAB_TRAY_ANIMATION"

/**
 * Deep links in the form of `fenix://host` open different parts of the app.
 */
class HomeDeepLinkIntentProcessor(
    private val activity: HomeActivity,
    private val showAddSearchWidgetPrompt: (Activity) -> Unit = ::maybeShowAddSearchWidgetPrompt,
) : HomeIntentProcessor {
    private val logger = Logger("DeepLinkIntentProcessor")

    override fun process(intent: Intent, navController: NavController, out: Intent, settings: AppSettings): Boolean {
        val scheme = intent.scheme?.equals(BuildConfig.DEEP_LINK_SCHEME, ignoreCase = true) ?: return false
        return if (scheme) {
            intent.data?.let { handleDeepLink(it, intent.extras, settings, navController) }
            true
        } else {
            false
        }
    }

    @Suppress("CyclomaticComplexMethod")
    private fun handleDeepLink(
        deepLink: Uri,
        extras: Bundle?,
        settings: AppSettings,
        navController: NavController,
    ) {
        handleDeepLinkSideEffects(deepLink, extras, settings, navController)

        val globalDirections = when (deepLink.host) {
            "home", "enable_private_browsing" -> GlobalDirections.Home
            "urls_bookmarks" -> GlobalDirections.Bookmarks
            "urls_history" -> GlobalDirections.History
            "settings" -> GlobalDirections.Settings
            "turn_on_sync" -> GlobalDirections.Sync
            "settings_search_engine" -> GlobalDirections.SearchEngine
            "settings_accessibility" -> GlobalDirections.Accessibility
            "settings_delete_browsing_data" -> GlobalDirections.DeleteData
            "settings_addon_manager" -> GlobalDirections.SettingsAddonManager
            "settings_logins" -> GlobalDirections.SettingsLogins
            "settings_tracking_protection" -> GlobalDirections.SettingsTrackingProtection
            // We'd like to highlight views within the fragment
            // https://github.com/mozilla-mobile/fenix/issues/11856
            // The current version of UI has these features in more complex screens.
            "settings_privacy" -> GlobalDirections.Settings
            "settings_wallpapers" -> GlobalDirections.WallpaperSettings
            "home_collections" -> GlobalDirections.Home
            "settings_private_browsing" -> GlobalDirections.SettingsPrivateBrowsing
            "settings_app_icon" -> GlobalDirections.SettingsAppIcon
            "settings_ai_controls" -> GlobalDirections.SettingsAIControls

            else -> return
        }

        if (!navController.alreadyOnDestination(globalDirections.destinationId)) {
            navController.navigate(globalDirections.navDirections)
        }
    }

    /**
     * Handle links that require more than just simple navigation.
     */
    private fun handleDeepLinkSideEffects(
        deepLink: Uri,
        extras: Bundle?,
        settings: AppSettings,
        navController: NavController,
    ) {
        when (deepLink.host) {
            "home" -> {
                if (extras?.containsKey(EXTRA_TAB_TRAY_ANIMATION) == true) {
                    val tabTrayAnimationPreference = extras.getBoolean(
                        EXTRA_TAB_TRAY_ANIMATION,
                        settings.tabManagerOpeningAnimationEnabled,
                    )
                    settings.tabManagerOpeningAnimationEnabled = tabTrayAnimationPreference
                }
            }
            "enable_private_browsing" -> {
                activity.browsingModeManager.mode = BrowsingMode.Private
            }
            "make_default_browser" -> {
                activity.openSetDefaultBrowserOption(
                    from = BrowserDirection.FromGlobal,
                    flags = EngineSession.LoadUrlFlags.external(),
                )
            }
            "open" -> {
                val url = deepLink.getQueryParameter("url")
                if (url == null || !url.startsWith("https://")) {
                    logger.info("Not opening deep link: $url")
                    return
                }

                @Suppress("DEPRECATION")
                activity.openToBrowserAndLoad(
                    url,
                    newTab = true,
                    from = BrowserDirection.FromGlobal,
                    flags = EngineSession.LoadUrlFlags.external(),
                )
            }
            "settings_notifications" -> {
                val intent = notificationSettings(activity)
                activity.startActivity(intent)
            }
            "install_search_widget" -> {
                showAddSearchWidgetPrompt(activity)
                return
            }
            "share_sheet" -> showShareSheet(deepLink, navController)
        }
    }

    private fun notificationSettings(context: Context, channel: String? = null) =
        Intent().apply {
            action = channel?.let {
                putExtra(Settings.EXTRA_CHANNEL_ID, it)
                Settings.ACTION_CHANNEL_NOTIFICATION_SETTINGS
            } ?: Settings.ACTION_APP_NOTIFICATION_SETTINGS
            putExtra(Settings.EXTRA_APP_PACKAGE, context.packageName)
        }

    private fun showShareSheet(deepLink: Uri, navController: NavController) {
        val url = deepLink.getQueryParameter("url")
        val title = deepLink.getQueryParameter("title").orEmpty()
        val text = deepLink.getQueryParameter("text").orEmpty()
        val subject = deepLink.getQueryParameter("subject").orEmpty()
        if (!url.isNullOrEmpty() && url.startsWith("https://")) {
            val shareData = arrayOf(ShareData(url = url, title = title, text = text))
            val direction = NavGraphDirections.actionGlobalShareFragment(
                data = shareData,
                shareSubject = subject,
                showPage = false,
                sessionId = null,
            )
            navController.navigate(direction)
        } else {
            logger.error("Invalid or missing URL for share_sheet deep link")
        }
    }
}
