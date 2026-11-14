/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.intent

import android.content.Intent
import androidx.navigation.NavController
import mozilla.components.browser.state.selector.findTab
import mozilla.components.support.utils.SwitchTabIntent
import org.mozilla.fenix.BrowserDirection
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.utils.Settings

/**
 * When the media notification is clicked we need to switch to the tab where the audio/video is playing. This intent has
 * the following informations: action - [SwitchTabIntent.ACTION_SWITCH_TAB] extra string for the tab
 * id - [SwitchTabIntent.EXTRA_TAB_ID]
 */
class OpenSpecificTabIntentProcessor(private val activity: HomeActivity) : HomeIntentProcessor {

    override fun process(intent: Intent, navController: NavController, out: Intent, settings: Settings): Boolean {
        if (intent.action == getAction()) {
            val browserStore = activity.components.core.store
            val tabId = intent.extras?.getString(getTabId())

            // Technically the additional lookup here isn't necessary, but this way we
            // can make sure that we never try and select a custom tab by mistake.
            val session = tabId?.let { browserStore.state.findTab(tabId) }
            if (session != null) {
                activity.components.useCases.tabsUseCases.selectTab(tabId)
                activity.openToBrowser(BrowserDirection.FromGlobal)
                return true
            }
        }

        return false
    }
}

private fun getAction(): String {
    return SwitchTabIntent.ACTION_SWITCH_TAB
}

private fun getTabId(): String {
    return SwitchTabIntent.EXTRA_TAB_ID
}
