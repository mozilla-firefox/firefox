/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.strip

import androidx.annotation.IdRes
import androidx.annotation.VisibleForTesting
import androidx.fragment.app.Fragment
import androidx.navigation.NavDirections
import androidx.navigation.fragment.findNavController
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.flow.map
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import mozilla.components.lib.state.ext.flowScoped
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.nav

/**
 * Observes selected tab changes during [fragment]'s view lifecycle. Navigates using [directions] when the selected
 * tab's URL requires switching between home and browser.
 *
 * @param fragment The [Fragment] whose view lifecycle and navigation controller drive the observation.
 * @param store The [BrowserStore] whose selected tab is observed.
 * @param currentDestinationId The destination the fragment must be on for the navigation to run.
 * @param directions The navigation to perform.
 */
internal fun observeSelectedTabForNavigation(
    fragment: Fragment,
    store: BrowserStore,
    @IdRes currentDestinationId: Int,
    directions: NavDirections,
) {
    store.flowScoped(fragment.viewLifecycleOwner, Dispatchers.Main) { flow ->
        flow
            .map { it.selectedTabId }
            .distinctUntilChanged()
            .drop(1)
            .collect {
                val url = store.state.selectedTab?.content?.url ?: return@collect
                val currentDestinationIsHomepage = currentDestinationId == R.id.homeFragment
                if (shouldNavigate(url, currentDestinationIsHomepage)) {
                    fragment.findNavController().nav(currentDestinationId, directions)
                }
            }
    }
}

/**
 * Whether selecting a tab with [url] should switch between the homepage and the browser: navigation is needed when the
 * selected tab is a homepage tab while the current destination is the browser, or vice versa.
 *
 * @param url The selected tab's URL.
 * @param currentDestinationIsHomepage Whether the fragment is currently on the homepage.
 */
@VisibleForTesting
internal fun shouldNavigate(url: String, currentDestinationIsHomepage: Boolean): Boolean {
    val isHomepageTab = url == ABOUT_HOME_URL
    return (isHomepageTab && !currentDestinationIsHomepage) || (!isHomepageTab && currentDestinationIsHomepage)
}
