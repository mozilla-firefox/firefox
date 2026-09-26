/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser

import android.content.Context
import android.view.ViewGroup
import androidx.annotation.VisibleForTesting
import androidx.lifecycle.LifecycleOwner
import androidx.navigation.NavController
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.mapNotNull
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.app.links.AppLinksUseCases
import mozilla.components.lib.state.ext.flowScoped
import mozilla.components.support.base.feature.LifecycleAwareFeature
import mozilla.components.support.ktx.kotlin.tryGetHostFromUrl
import mozilla.components.support.ktx.kotlinx.coroutines.flow.ifAnyChanged
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.infobanner.DynamicInfoBanner
import org.mozilla.fenix.browser.infobanner.InfoBanner
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.utils.Settings

/** Displays an [InfoBanner] when a user visits a website that can be opened in an installed native app. */
class OpenInAppOnboardingObserver(
    private val context: Context,
    private val store: BrowserStore,
    private val lifecycleOwner: LifecycleOwner,
    private val navController: NavController,
    private val settings: Settings,
    private val appLinksUseCases: AppLinksUseCases,
    private val container: ViewGroup,
    internal val shouldScrollWithTopToolbar: Boolean = false,
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : LifecycleAwareFeature {
    private var scope: CoroutineScope? = null
    private var currentUrl: String? = null
    private var sessionDomainForDisplayedBanner: String? = null

    @VisibleForTesting internal var infoBanner: InfoBanner? = null

    override fun start() {
        scope =
            store.flowScoped(lifecycleOwner, mainDispatcher) { flow ->
                flow
                    .mapNotNull { state ->
                        state.selectedTab
                    }
                    .ifAnyChanged { tab ->
                        arrayOf(tab.content.url, tab.content.loading)
                    }
                    .collect { tab ->
                        if (tab.content.url != currentUrl) {
                            sessionDomainForDisplayedBanner?.let {
                                if (tab.content.url.tryGetHostFromUrl() != it) {
                                    infoBanner?.dismiss()
                                }
                            }
                            currentUrl = tab.content.url
                        } else {
                            // Loading state has changed
                            maybeShowOpenInAppBanner(tab.content.url, tab.content.loading)
                        }
                    }
            }
    }

    override fun stop() {
        scope?.cancel()
    }

    private fun maybeShowOpenInAppBanner(url: String, loading: Boolean) {
        if (!settings.inAppMessagesEnabled) {
            return
        }

        if (loading || settings.shouldOpenLinksInApp() || !settings.shouldShowOpenInAppCfr) {
            return
        }

        val appLink = appLinksUseCases.appLinkRedirect
        if (appLink(url).hasExternalApp()) {
            infoBanner = createInfoBanner()
            infoBanner?.showBanner()
            sessionDomainForDisplayedBanner = url.tryGetHostFromUrl()
            settings.shouldShowOpenInAppBanner = false
        }
    }

    @VisibleForTesting
    internal fun createInfoBanner(): DynamicInfoBanner {
        val appName = context.getString(R.string.app_name)
        return DynamicInfoBanner(
            context = context,
            message = context.getString(R.string.open_in_app_cfr_info_message_2, appName),
            dismissText = context.getString(R.string.open_in_app_cfr_negative_button_text),
            actionText = context.getString(R.string.open_in_app_cfr_positive_button_text),
            container = container,
            shouldScrollWithTopToolbar = shouldScrollWithTopToolbar,
        ) {
            val directions =
                BrowserFragmentDirections.actionBrowserFragmentToSettingsFragment(
                    preferenceToScrollTo = context.getString(R.string.pref_key_open_links_in_apps)
                )
            navController.nav(R.id.browserFragment, directions)
        }
    }
}
