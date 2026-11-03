/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.biometrics

import android.content.Context
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import mozilla.components.browser.state.selector.privateTabs
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.lib.auth.canUseBiometricFeature
import org.mozilla.focus.GleanMetrics.TabCount
import org.mozilla.focus.ext.components
import org.mozilla.focus.ext.settings
import org.mozilla.focus.state.AppAction
import org.mozilla.focus.state.AppStore
import org.mozilla.focus.topsites.DefaultTopSitesStorage

/**
 * Observer that locks the app when it goes to the background or is paused, based on biometric settings and screen
 * contents.
 *
 * @param context The application context.
 * @param browserStore The store that holds the browser state.
 * @param appStore The store that holds the app state.
 */
class LockObserver(
    private val context: Context,
    private val browserStore: BrowserStore,
    private val appStore: AppStore,
) : DefaultLifecycleObserver {

    override fun onCreate(owner: LifecycleOwner) {
        super.onCreate(owner)
        checkAndLockApp(owner)
    }

    override fun onPause(owner: LifecycleOwner) {
        super.onPause(owner)
        checkAndLockApp(owner)
        recordTabCount()
    }

    /** Records the count of private tabs when the app goes to the background. */
    private fun recordTabCount() {
        val tabCount = browserStore.state.privateTabs.size.toLong()
        TabCount.appBackgrounded.accumulateSamples(listOf(tabCount))
    }

    /**
     * Checks if the app can be locked based on biometric settings, SDK version and hardware capabilities.
     *
     * @return True if the app can be locked, false otherwise.
     */
    private fun canLockApp(): Boolean {
        return context.settings.shouldUseBiometrics() && context.canUseBiometricFeature()
    }

    override fun onResume(owner: LifecycleOwner) {
        super.onResume(owner)
        checkAndLockApp(owner)
    }

    /**
     * Checks conditions and locks the app if necessary.
     *
     * @param owner The lifecycle owner.
     */
    private fun checkAndLockApp(owner: LifecycleOwner) {
        owner.lifecycleScope.launch(Dispatchers.IO) {
            if (canLockApp()) {
                if (browserStore.state.privateTabs.isNotEmpty()) {
                    appStore.dispatch(AppAction.Lock())
                    return@launch
                }

                val topSitesList =
                    context.components.topSitesStorage.getTopSites(
                        targetSitesCount = DefaultTopSitesStorage.TOP_SITES_MAX_LIMIT,
                        frecencyConfig = null,
                    )

                if (topSitesList.isNotEmpty()) {
                    appStore.dispatch(AppAction.Lock())
                }
            }
        }
    }
}
