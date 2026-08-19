/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.accounts

import android.app.Activity
import androidx.navigation.findNavController
import java.lang.ref.WeakReference
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.webextension.WebExtensionRuntime
import mozilla.components.feature.accounts.FxaCapability
import mozilla.components.feature.accounts.FxaWebChannelFeature
import mozilla.components.feature.accounts.FxaWebChannelFeature.Companion.WebChannelCommand
import mozilla.components.service.fxa.ServerConfig
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.support.base.feature.LifecycleAwareFeature
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.customtabs.ExternalAppBrowserActivity
import org.mozilla.fenix.ext.isIntentInternal
import org.mozilla.fenix.ext.nav

/**
 * An integration class that connects the [FxaWebChannelFeature] with custom web command handling for Fenix UI
 * affordances. For example, "log out" messages from web content will close the view.
 *
 * @param customTabSessionId see [FxaWebChannelFeature.customTabSessionId].
 * @param runtime see [FxaWebChannelFeature.runtime].
 * @param store see [FxaWebChannelFeature.store].
 * @param serverConfig see [FxaWebChannelFeature.serverConfig].
 * @param accountManager see [FxaWebChannelFeature.accountManager].
 * @param activityRef a reference to provide the [Activity] to dismiss.
 */
class FxaWebChannelIntegration(
    customTabSessionId: String?,
    runtime: WebExtensionRuntime,
    store: BrowserStore,
    serverConfig: ServerConfig,
    private val accountManager: FxaAccountManager,
    private val activityRef: WeakReference<Activity?>,
) : LifecycleAwareFeature {
    private val logger = Logger("FxaWebChannelIntegration")
    private val feature by lazy {
        FxaWebChannelFeature(
            customTabSessionId = customTabSessionId,
            runtime = runtime,
            store = store,
            accountManager = accountManager,
            serverConfig = serverConfig,
            fxaCapabilities = setOf(FxaCapability.CHOOSE_WHAT_TO_SYNC, FxaCapability.PAIRING_V2),
            onCommandExecuted = ::commandRouter,
        )
    }

    override fun start() {
        feature.start()
    }

    override fun stop() {
        feature.stop()
    }

    private fun commandRouter(command: WebChannelCommand) =
        when (command) {
            WebChannelCommand.SYNC_PREFERENCES -> handleWebCommandSyncPreferences()

            WebChannelCommand.LOGOUT,
            WebChannelCommand.DELETE_ACCOUNT -> handleWebCommandLogOut()

            else -> {
                // noop
            }
        }

    private fun handleWebCommandSyncPreferences() {
        val hasAuthAccount = accountManager.authenticatedAccount() != null
        logger.info("Received ${WebChannelCommand.SYNC_PREFERENCES}, responding with: $hasAuthAccount")
        if (!hasAuthAccount) {
            return
        }

        // We could get this command while the activity is no longer available, so we only take it
        // when we are about to use it.
        val activity = activityRef.get() ?: return
        val navController = activity.findNavController(R.id.container)

        navController.nav(
            R.id.browserFragment,
            BrowserFragmentDirections.actionGlobalAccountSettingsFragment(),
        )
    }

    private fun handleWebCommandLogOut() {
        logger.info("Received ${WebChannelCommand.DELETE_ACCOUNT} or ${WebChannelCommand.LOGOUT}")
        // We could get this command while the activity is no longer available, so we only take it
        // when we are about to use it.
        val activity = activityRef.get() ?: return

        val isInternalIntent = activity.isIntentInternal()
        val isExternalTabActivity = activity is ExternalAppBrowserActivity

        // We use a Custom Tab to show the account, but to avoid mistakenly closing a real
        // Custom Tab, we need to make sure the intent is internal.
        if (!isExternalTabActivity && !isInternalIntent) {
            return
        }

        activity.finish()
    }
}
