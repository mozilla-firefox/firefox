/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ext

import android.app.Activity
import android.app.role.RoleManager
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.view.View
import android.view.WindowManager
import androidx.annotation.DrawableRes
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.content.res.AppCompatResources
import androidx.navigation.NavDestination
import androidx.navigation.NavDirections
import androidx.navigation.findNavController
import mozilla.components.concept.base.crash.Breadcrumb
import mozilla.components.concept.engine.EngineSession
import mozilla.components.feature.intent.ext.getSessionId
import mozilla.components.support.ktx.android.content.getColorFromAttr
import mozilla.components.support.utils.EXTRA_ACTIVITY_REFERRER_PACKAGE
import mozilla.components.support.utils.SafeIntent
import mozilla.components.support.utils.ext.SETTINGS_SELECT_OPTION_KEY
import mozilla.components.support.utils.ext.SETTINGS_SHOW_FRAGMENT_ARGS
import mozilla.components.support.utils.toSafeIntent
import org.mozilla.fenix.BrowserDirection
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.addons.AddonDetailsFragmentDirections
import org.mozilla.fenix.addons.AddonPermissionsDetailsFragmentDirections
import org.mozilla.fenix.addons.AddonsManagementFragmentDirections
import org.mozilla.fenix.bookmarks.BookmarkFragmentDirections
import org.mozilla.fenix.components.menu.MenuDialogFragmentDirections
import org.mozilla.fenix.customtabs.EXTRA_IS_SANDBOX_CUSTOM_TAB
import org.mozilla.fenix.customtabs.ExternalAppBrowserActivity
import org.mozilla.fenix.debugsettings.gleandebugtools.GleanDebugToolsFragmentDirections
import org.mozilla.fenix.exceptions.trackingprotection.TrackingProtectionExceptionsFragmentDirections
import org.mozilla.fenix.home.HomeFragmentDirections
import org.mozilla.fenix.library.history.HistoryFragmentDirections
import org.mozilla.fenix.library.historymetadata.HistoryMetadataGroupFragmentDirections
import org.mozilla.fenix.library.recentlyclosed.RecentlyClosedFragmentDirections
import org.mozilla.fenix.search.SearchDialogFragmentDirections
import org.mozilla.fenix.settings.HttpsOnlyFragmentDirections
import org.mozilla.fenix.settings.SettingsFragmentDirections
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.settings.TrackingProtectionFragmentDirections
import org.mozilla.fenix.settings.about.AboutFragmentDirections
import org.mozilla.fenix.settings.doh.DohSettingsFragmentDirections
import org.mozilla.fenix.settings.logins.fragment.SavedLoginsAuthFragmentDirections
import org.mozilla.fenix.settings.search.SaveSearchEngineFragmentDirections
import org.mozilla.fenix.settings.search.SearchEngineFragmentDirections
import org.mozilla.fenix.settings.studies.StudiesFragmentDirections
import org.mozilla.fenix.settings.wallpaper.WallpaperSettingsFragmentDirections
import org.mozilla.fenix.share.AddNewDeviceFragmentDirections
import org.mozilla.fenix.tabstray.ui.TabManagementFragmentDirections
import org.mozilla.fenix.trackingprotection.TrackingProtectionPanelDialogFragmentDirections
import org.mozilla.fenix.translations.TranslationsDialogFragmentDirections
import org.mozilla.fenix.translations.preferences.downloadlanguages.DownloadLanguagesPreferenceFragmentDirections
import org.mozilla.fenix.webcompat.ui.WebCompatReporterFragmentDirections
import java.security.InvalidParameterException
import com.google.android.material.R as materialR

/**
 * Attempts to call immersive mode using the View to hide the status bar and navigation buttons.
 *
 * We don't use the equivalent function from Android Components because the stable flag messes
 * with the toolbar. See #1998 and #3272.
 */
@Deprecated(
    message = "Use the Android Component implementation instead.",
    replaceWith = ReplaceWith(
        "enterToImmersiveMode()",
        "mozilla.components.support.ktx.android.view.enterToImmersiveMode",
    ),
)
fun Activity.enterToImmersiveMode() {
    window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    // This will be addressed on https://github.com/mozilla-mobile/fenix/issues/17804
    @Suppress("DEPRECATION")
    window.decorView.systemUiVisibility = (
        View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            or View.SYSTEM_UI_FLAG_FULLSCREEN
            or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        )
}

fun Activity.breadcrumb(
    message: String,
    data: Map<String, String> = emptyMap(),
) {
    components.analytics.crashReporter.recordCrashBreadcrumb(
        Breadcrumb(
            category = this::class.java.simpleName,
            message = message,
            data = data + mapOf(
                "instance" to this.hashCode().toString(),
            ),
            level = Breadcrumb.Level.INFO,
        ),
    )
}

/**
 * Opens Android's Manage Default Apps Settings if possible.
 * Otherwise navigates to the Sumo article indicating why it couldn't open it.
 *
 * @param from fallback direction in case, couldn't open the setting.
 * @param flags fallback flags for when opening the Sumo article page.
 * @param useCustomTab fallback to open the Sumo article in a custom tab.
 */
fun Activity.openSetDefaultBrowserOption(
    from: BrowserDirection = BrowserDirection.FromSettings,
    flags: EngineSession.LoadUrlFlags = EngineSession.LoadUrlFlags.none(),
    useCustomTab: Boolean = false,
) {
    when {
        Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q -> {
            getSystemService(RoleManager::class.java).also {
                if (it.isRoleAvailable(RoleManager.ROLE_BROWSER) && !it.isRoleHeld(
                        RoleManager.ROLE_BROWSER,
                    )
                ) {
                    startActivityForResult(
                        it.createRequestRoleIntent(RoleManager.ROLE_BROWSER),
                        REQUEST_CODE_BROWSER_ROLE,
                    )
                } else {
                    navigateToDefaultBrowserAppsSettings(
                        useCustomTab = useCustomTab,
                        from = from,
                        flags = flags,
                    )
                }
            }
        }
        else -> {
            navigateToDefaultBrowserAppsSettings(
                useCustomTab = useCustomTab,
                from = from,
                flags = flags,
            )
        }
    }
}

/**
 * Checks if the app can prompt the user to set it as the default browser.
 *
 * From Android 10, a new method to prompt the user to set default apps has been introduced.
 * This method checks if the app can prompt the user to set it as the default browser
 * based on the Android version and the availability of the ROLE_BROWSER.
 */
fun Context.isDefaultBrowserPromptSupported(): Boolean {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        getSystemService(RoleManager::class.java).also {
            if (it.isRoleAvailable(RoleManager.ROLE_BROWSER) && !it.isRoleHeld(
                    RoleManager.ROLE_BROWSER,
                )
            ) {
                return true
            }
        }
    }
    return false
}

private fun Activity.navigateToDefaultBrowserAppsSettings(
    from: BrowserDirection,
    flags: EngineSession.LoadUrlFlags,
    useCustomTab: Boolean,
) {
    val intent = Intent(Settings.ACTION_MANAGE_DEFAULT_APPS_SETTINGS).apply {
        putExtra(SETTINGS_SELECT_OPTION_KEY, DEFAULT_BROWSER_APP_OPTION)
        putExtra(
            SETTINGS_SHOW_FRAGMENT_ARGS,
            Bundle().apply { putString(SETTINGS_SELECT_OPTION_KEY, DEFAULT_BROWSER_APP_OPTION) },
        )
    }
    startExternalActivitySafe(
        intent = intent,
        onActivityNotPresent = {
            openDefaultBrowserSumoPage(useCustomTab = useCustomTab, from = from, flags = flags)
        },
    )
}

private fun Activity.openDefaultBrowserSumoPage(
    useCustomTab: Boolean,
    from: BrowserDirection,
    flags: EngineSession.LoadUrlFlags,
) {
    val sumoDefaultBrowserUrl = SupportUtils.getGenericSumoURLForTopic(
        topic = SupportUtils.SumoTopic.SET_AS_DEFAULT_BROWSER,
    )
    if (useCustomTab) {
        SupportUtils.launchSandboxCustomTab(
            context = this,
            url = sumoDefaultBrowserUrl,
        )
    } else {
        val directions = getNavDirections(from)
        if (directions != null) {
            val navController = findNavController(R.id.container)
            navController.navigate(directions)
        }

        components.useCases.fenixBrowserUseCases.loadUrlOrSearch(
            searchTermOrURL = sumoDefaultBrowserUrl,
            newTab = true,
            forceSearch = false,
            searchEngine = null,
            flags = flags,
        )
    }
}

/**
 * Sets the icon for the back (up) navigation button.
 * @param icon The resource id of the icon.
 */
fun Activity.setNavigationIcon(
    @DrawableRes icon: Int,
) {
    (this as? AppCompatActivity)?.supportActionBar?.let {
        it.setDisplayHomeAsUpEnabled(true)
        val navigationIcon = AppCompatResources.getDrawable(this, icon)
        it.setHomeAsUpIndicator(navigationIcon)
        it.setHomeActionContentDescription(R.string.action_bar_up_description)
    }
}

/**
 * Delegate to the relevant 'get nav directions' function based on the given [Activity].
 *
 * @param from The [BrowserDirection] to indicate which fragment the browser is being opened from.
 * @param customTabSessionId Optional custom tab session ID if navigating from a custom tab.
 *
 * @return the [NavDirections] for the given [Activity].
 * @throws IllegalArgumentException if the given [Activity] is not supported.
 */
fun Activity.getNavDirections(
    from: BrowserDirection,
    customTabSessionId: String? = null,
): NavDirections? = when (this) {
    is ExternalAppBrowserActivity -> getExternalAppBrowserNavDirections(from, customTabSessionId)
    is HomeActivity -> getHomeNavDirections(from)
    else -> throw IllegalArgumentException("$this is not supported")
}

private fun Activity.getExternalAppBrowserNavDirections(
    from: BrowserDirection,
    customTabSessionId: String?,
): NavDirections? {
    if (customTabSessionId == null) {
        finishAndRemoveTask()
        return null
    }

    return when (from) {
        BrowserDirection.FromGlobal ->
            NavGraphDirections.actionGlobalExternalAppBrowser(
                activeSessionId = customTabSessionId,
                webAppManifestUrl = intent.toSafeIntent().dataString,
                isSandboxCustomTab = intent.getBooleanExtra(EXTRA_IS_SANDBOX_CUSTOM_TAB, false),
            )

        else -> throw InvalidParameterException(
            "Tried to navigate to ExternalAppBrowserFragment from $from",
        )
    }
}

private fun getHomeNavDirections(
    from: BrowserDirection,
): NavDirections = when (from) {
    BrowserDirection.FromGlobal -> NavGraphDirections.actionGlobalBrowser()

    BrowserDirection.FromHome -> HomeFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromWallpaper -> WallpaperSettingsFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromSearchDialog -> SearchDialogFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromSettings -> SettingsFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromBookmarks -> BookmarkFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromHistory -> HistoryFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromGleanDebugToolsFragment -> GleanDebugToolsFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromHistoryMetadataGroup -> HistoryMetadataGroupFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromTrackingProtectionExceptions ->
        TrackingProtectionExceptionsFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromHttpsOnlyMode -> HttpsOnlyFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromDnsOverHttps -> DohSettingsFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromAbout -> AboutFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromTrackingProtection -> TrackingProtectionFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromTrackingProtectionDialog ->
        TrackingProtectionPanelDialogFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromSavedLoginsFragment -> SavedLoginsAuthFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromAddNewDeviceFragment -> AddNewDeviceFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromSearchEngineFragment -> SearchEngineFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromSaveSearchEngineFragment -> SaveSearchEngineFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromAddonDetailsFragment -> AddonDetailsFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromAddonPermissionsDetailsFragment ->
        AddonPermissionsDetailsFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromTabManager -> TabManagementFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromRecentlyClosed -> RecentlyClosedFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromStudiesFragment -> StudiesFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromAddonsManagementFragment -> AddonsManagementFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromTranslationsDialogFragment -> TranslationsDialogFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromMenuDialogFragment -> MenuDialogFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromDownloadLanguagesPreferenceFragment ->
        DownloadLanguagesPreferenceFragmentDirections.actionGlobalBrowser()

    BrowserDirection.FromWebCompatReporterFragment ->
        WebCompatReporterFragmentDirections.actionGlobalBrowser()
}

const val REQUEST_CODE_BROWSER_ROLE = 1
const val SETTINGS_SELECT_OPTION_KEY = ":settings:fragment_args_key"
const val SETTINGS_SHOW_FRAGMENT_ARGS = ":settings:show_fragment_args"
const val DEFAULT_BROWSER_APP_OPTION = "default_browser"
const val EXTERNAL_APP_BROWSER_INTENT_SOURCE = "CUSTOM_TAB"

/**
 * Depending on the [Activity], maybe derive the source of the given [intent].
 *
 * @param intent the [SafeIntent] to derive the source from.
 * @throws IllegalArgumentException if the given [Activity] is not supported.
 */
fun Activity.getIntentSource(intent: SafeIntent): String? = when (this) {
    is ExternalAppBrowserActivity -> EXTERNAL_APP_BROWSER_INTENT_SOURCE
    is HomeActivity -> getHomeIntentSource(intent)
    else -> throw IllegalArgumentException("$this is not supported")
}

private fun getHomeIntentSource(intent: SafeIntent): String? {
    return when {
        intent.isLauncherIntent -> HomeActivity.APP_ICON
        intent.action == Intent.ACTION_VIEW -> "LINK"
        else -> null
    }
}

/**
 * Check if the intent is coming from within this application itself or from an external one
 * when processed through the `InternalReceiverActivity`.
 */
fun Activity.isIntentInternal(): Boolean {
    val safeIntent = SafeIntent(intent)
    return safeIntent.getStringExtra(EXTRA_ACTIVITY_REFERRER_PACKAGE) == this.packageName
}

/**
 * Depending on the [Activity], maybe derive the session ID of the given [intent].
 *
 * @param intent the [SafeIntent] to derive the session ID from.
 * @throws IllegalArgumentException if the given [Activity] is not supported.
 */
fun Activity.getIntentSessionId(intent: SafeIntent): String? = when (this) {
    is ExternalAppBrowserActivity -> getExternalAppBrowserIntentSessionId(intent)
    is HomeActivity -> null
    else -> throw IllegalArgumentException("$this is not supported")
}

private fun getExternalAppBrowserIntentSessionId(intent: SafeIntent) = intent.getSessionId()

/**
 * Get the breadcrumb message for the [Activity].
 *
 * @param destination the [NavDestination] required to provide the destination ID.
 * @throws IllegalArgumentException if the given [Activity] is not supported.
 */
fun Activity.getBreadcrumbMessage(destination: NavDestination): String = when (this) {
    is ExternalAppBrowserActivity -> getExternalAppBrowserBreadcrumbMessage(destination.id)
    is HomeActivity -> getHomeBreadcrumbMessage(destination.id)
    else -> throw IllegalArgumentException("$this is not supported")
}

private fun Activity.getExternalAppBrowserBreadcrumbMessage(destinationId: Int): String {
    val fragmentName = resources.getResourceEntryName(destinationId)
    return "Changing to fragment $fragmentName, isCustomTab: true"
}

private fun Activity.getHomeBreadcrumbMessage(destinationId: Int): String {
    val fragmentName = resources.getResourceEntryName(destinationId)
    return "Changing to fragment $fragmentName, isCustomTab: false"
}
