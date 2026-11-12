/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import android.content.Context
import androidx.core.net.toUri
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.accounts.FirefoxAccountsAuthFeature
import mozilla.components.feature.app.links.AppLinksInterceptor
import mozilla.components.service.fxa.manager.FxaAccountManager
import org.mozilla.fenix.components.accounts.EntrypointTrackingAuthFeature
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.utils.PersistStoryUTMRequestInterceptor
import org.mozilla.fenix.utils.Settings

/** Component group which encapsulates foreground-friendly services. */
class Services(
    private val context: Context,
    private val store: BrowserStore,
    private val accountManager: FxaAccountManager,
    private val settings: Settings,
) {
    val accountsAuthFeature by lazy {
        EntrypointTrackingAuthFeature(
            feature =
                FirefoxAccountsAuthFeature(accountManager, FxaServer.REDIRECT_URL) { context, authUrl ->
                    var url = authUrl
                    if (settings.useReactFxAServer) {
                        url =
                            url.toUri()
                                .buildUpon()
                                .appendQueryParameter("forceExperiment", "generalizedReactApp")
                                .appendQueryParameter("forceExperimentGroup", "react")
                                .build()
                                .toString()
                    }
                    CoroutineScope(Dispatchers.Main).launch {
                        val intent = SupportUtils.createAuthCustomTabIntent(context, url)
                        context.startActivity(intent)
                    }
                },
            settings = settings,
        )
    }

    val appLinksInterceptor by lazy {
        AppLinksInterceptor(
            context = context,
            launchInApp = { _ -> context.components.settings.shouldOpenLinksInApp() },
            launchFromInterceptor = false,
            store = store,
        )
    }

    val storyUTMRequestInterceptor by lazy {
        PersistStoryUTMRequestInterceptor()
    }
}
